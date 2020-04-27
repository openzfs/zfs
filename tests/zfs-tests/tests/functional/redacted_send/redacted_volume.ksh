#!/bin/ksh

#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# http://www.illumos.org/license/CDDL.
#

#
# Copyright (c) 2018 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/redacted_send/redacted.kshlib

#
# Description:
# Verify that redacted send works on volumes.
#
# Strategy:
# 1. Write to a volume, then make a clone of that volume.
# 2. Receive a redacted stream that sends all blocks.
# 3. Receive a redacted stream that redacts the first half of the written area.
#

typeset ds_name="volume"
typeset sendvol="$POOL/$ds_name"
typeset recvvol="$POOL2/$ds_name"
typeset clone="$POOL/${ds_name}_clone"
typeset tmpdir="$(get_prop mountpoint $POOL)/tmp"
typeset stream=$(mktemp $tmpdir/stream.XXXX)
typeset send_file="/dev/zvol/$sendvol"
typeset recv_file="/dev/zvol/$recvvol"
typeset clone_file="/dev/zvol/$clone"

log_onexit redacted_cleanup $sendvol $recvvol

log_must zfs create -b 8k -V 1g $sendvol
sleep 10
log_must zpool export $POOL
log_must zpool import $POOL
udevadm settle
if ! is_disk_device $send_file; then
	udevadm settle
	for t in 10 5 3 2 1; do
		log_note "Polling $t seconds for device file."
		udevadm settle
		sleep $t
		is_disk_device $send_file && break
	done
fi
log_must dd if=/dev/urandom of=$send_file bs=8k count=64
log_must zfs snapshot $sendvol@snap
log_must zfs clone $sendvol@snap $clone
log_must zfs snapshot $clone@snap

log_must set_tunable32 ALLOW_REDACTED_DATASET_MOUNT 1
log_must zfs redact $sendvol@snap book1 $clone@snap
log_must eval "zfs send --redact book1 $sendvol@snap >$stream"
log_must eval "zfs recv $recvvol <$stream"
sleep 10
log_must zpool export $POOL2
log_must zpool import $POOL2
udevadm settle
if ! is_disk_device $recv_file; then
	udevadm settle
	for t in 10 5 3 2 1; do
		log_note "Polling $t seconds for device file."
		udevadm settle
		sleep $t
		is_disk_device $recv_file && break
	done
fi
log_must dd if=$send_file of=$tmpdir/send.dd bs=8k count=64
log_must dd if=$recv_file of=$tmpdir/recv.dd bs=8k count=64
log_must diff $tmpdir/send.dd $tmpdir/recv.dd
log_must zfs destroy -R $recvvol

log_must dd if=/dev/urandom of=$clone_file bs=8k count=32
log_must zfs snapshot $clone@snap1
log_must zfs redact $sendvol@snap book2 $clone@snap1
log_must eval "zfs send --redact book2 $sendvol@snap >$stream"
log_must eval "zfs recv $recvvol <$stream"
sleep 10
log_must zpool export $POOL2
log_must zpool import $POOL2
udevadm settle
if ! is_disk_device $recv_file; then
	udevadm settle
	for t in 10 5 3 2 1; do
		log_note "Polling $t seconds for device file."
		udevadm settle
		sleep $t
		is_disk_device $recv_file && break
	done
fi
log_must dd if=$send_file of=$tmpdir/send.dd bs=8k count=32 skip=32
log_must dd if=$recv_file of=$tmpdir/recv.dd bs=8k count=32 skip=32
log_must diff $tmpdir/send.dd $tmpdir/recv.dd

log_pass "Redacted send works correctly with volumes."
