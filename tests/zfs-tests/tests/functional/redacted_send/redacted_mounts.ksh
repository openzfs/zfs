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
# Verify that received redacted datasets are not mounted by default, but
# can still be mounted after setting ALLOW_REDACTED_DATASET_MOUNT.
#
# Strategy:
# 1. Verify a received redacted stream isn't mounted by default.
# 2. Set ALLOW_REDACTED_DATASET_MOUNT and verify it can't be mounted
#    without the -f flag, but can with -f.
# 3. Receive a redacted volume.
# 4. Verify the device file isn't present until the kernel variable is set.
# 5. Verify the files in the send fs are also present in the recv fs.
#

typeset ds_name="mounts"
typeset sendfs="$POOL/$ds_name"
typeset sendvol="$sendfs/vol"
typeset recvfs="$POOL2/$ds_name"
typeset recvvol="$POOL2/vol"
typeset clone="$POOL/${ds_name}_clone"
typeset clonevol="${sendvol}_clone"
typeset tmpdir="$(get_prop mountpoint $POOL)/tmp"
typeset stream=$(mktemp $tmpdir/stream.XXXX)
setup_dataset $ds_name '' setup_mounts
typeset clone_mnt="$(get_prop mountpoint $clone)"
typeset send_mnt="$(get_prop mountpoint $sendfs)"
typeset recv_mnt="/$POOL2/$ds_name"
typeset recv_vol_file="/dev/zvol/$recvvol"

log_onexit redacted_cleanup $sendfs $recvfs $recvvol

log_must rm $clone_mnt/empty $clone_mnt/contents1
log_must dd if=/dev/urandom of=$clone_mnt/contents2 bs=512 count=1 conv=notrunc
log_must rm $clone_mnt/dir1/contents1
log_must rm -rf $clone_mnt/dir1/dir2
log_must dd if=/dev/urandom of=$clone_mnt/dir1/contents2 bs=512 count=1 \
    conv=notrunc
log_must dd if=/dev/urandom of=$clone_mnt/dir1/empty bs=512 count=1
log_must zfs snapshot $clone@snap1

log_must zfs redact $sendfs@snap book1 $clone@snap
log_must eval "zfs send --redact book1 $sendfs@snap >$stream"
log_must eval "zfs receive $recvfs <$stream"
log_mustnot ismounted $recvfs
log_mustnot mount_redacted $recvfs
log_mustnot ismounted $recvfs
log_must mount_redacted -f $recvfs
log_must ismounted $recvfs

# Verify that the send and recv fs both have the same files under their
# mountpoints by comparing find output with the name of the mountpoint
# deleted.
contents=$(log_must find $recv_mnt)
contents_orig=$(log_must find $send_mnt)
log_must diff <(echo ${contents//$recv_mnt/}) \
    <(echo ${contents_orig//$send_mnt/})
log_must zfs redact $sendvol@snap book2 $clonevol@snap
log_must eval "zfs send --redact book2 $sendvol@snap >$stream"
log_must eval "zfs receive $recvvol <$stream"
is_disk_device $recv_vol_file && log_fail "Volume device file should not exist."
log_must set_tunable32 ALLOW_REDACTED_DATASET_MOUNT 1
log_must zpool export $POOL2
log_must zpool import $POOL2
udevadm settle

# The device file isn't guaranteed to show up right away.
if ! is_disk_device $recv_vol_file; then
	udevadm settle
	for t in 10 5 3 2 1; do
		log_note "Polling $t seconds for device file."
		udevadm settle
		sleep $t
		is_disk_device $recv_vol_file && break
	done
fi
is_disk_device $recv_vol_file || log_fail "Volume device file should exist."

log_must dd if=/dev/urandom of=$send_mnt/dir1/contents1 bs=512 count=2
log_must rm $send_mnt/dir1/dir2/empty
log_must zfs snapshot $sendfs@snap2
log_must eval "zfs send -i $sendfs#book1 $sendfs@snap2 >$stream"
log_must eval "zfs receive $recvfs <$stream"
log_must mount_redacted -f $recvfs
log_must ismounted $recvfs
contents=$(log_must find $recv_mnt)
contents_orig=$(log_must find $send_mnt)
log_must diff <(echo ${contents//$recv_mnt/}) \
    <(echo ${contents_orig//$send_mnt/})

log_pass "Received redacted streams can be mounted."
