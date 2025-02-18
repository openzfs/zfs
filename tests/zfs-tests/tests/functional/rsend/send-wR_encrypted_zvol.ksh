#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# CDDL HEADER START
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
# CDDL HEADER END
#

#
# Copyright (c) 2018 by Datto Inc. All rights reserved.
# Copyright (c) 2020 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/rsend/rsend.kshlib

#
# DESCRIPTION:
# Verify that zvols with encryption=on can be sent and received with a raw
# send stream.
#
# STRATEGY:
# 1. Create a zvol with encryption on and put a filesystem on it
# 2. Copy a file into the zvol a few times and take a snapshot
# 3. Repeat step 2 a few times to create more snapshots
# 4. Send all snapshots in a recursive, raw send stream
# 5. Mount the received zvol and verify that all of the data there is correct
#

verify_runnable "both"

function cleanup
{
	ismounted $recvmnt $fstype && log_must umount $recvmnt
	ismounted $mntpnt $fstype && log_must umount $mntpnt
	[[ -d $recvmnt ]] && log_must rm -rf $keyfile
	[[ -d $mntpnt ]] && log_must rm -rf $keyfile
	destroy_dataset $TESTPOOL/recv "-r"
	destroy_dataset $TESTPOOL/$TESTVOL "-r"
	[[ -f $keyfile ]] && log_must rm $keyfile
	[[ -f $sendfile ]] && log_must rm $sendfile
}
log_onexit cleanup

log_assert "Verify zfs can receive raw, recursive send streams"

typeset keyfile=/$TESTPOOL/pkey
typeset snap_count=5
typeset zdev=$ZVOL_DEVDIR/$TESTPOOL/$TESTVOL
typeset mntpnt=$TESTDIR/$TESTVOL
typeset recvdev=$ZVOL_DEVDIR/$TESTPOOL/recv
typeset recvmnt=$TESTDIR/recvmnt
typeset sendfile=$TESTDIR/sendfile
typeset fstype=none

log_must eval "echo 'password' > $keyfile"

log_must zfs create -o dedup=on -o encryption=on -o keyformat=passphrase \
	-o keylocation=file://$keyfile -V 128M $TESTPOOL/$TESTVOL
block_device_wait

if is_linux; then
	# ext4 only supported on Linux
	log_must new_fs -t ext4 $zdev
	fstype=ext4
	typeset remount_ro="-o remount,ro"
	typeset remount_rw="-o remount,rw"
else
	log_must new_fs $zdev
	fstype=$NEWFS_DEFAULT_FS
	typeset remount_ro="-ur"
	typeset remount_rw="-uw"
fi
log_must mkdir -p $mntpnt
log_must mkdir -p $recvmnt
log_must mount $zdev $mntpnt

for ((i = 1; i <= $snap_count; i++)); do
	log_must dd if=/dev/urandom of=$mntpnt/file bs=1M count=1
	for ((j = 0; j < 10; j++)); do
		log_must cp $mntpnt/file $mntpnt/file$j
	done

	sync_all_pools
	log_must mount $remount_ro $zdev $mntpnt
	log_must zfs snap $TESTPOOL/$TESTVOL@snap$i
	log_must mount $remount_rw $zdev $mntpnt
done

log_must eval "zfs send -wR $TESTPOOL/$TESTVOL@snap$snap_count > $sendfile"
log_must eval "zfs recv $TESTPOOL/recv < $sendfile"
log_must zfs load-key $TESTPOOL/recv
block_device_wait

log_must mount $recvdev $recvmnt

hash1=$(cat $mntpnt/* | xxh128digest)
hash2=$(cat $recvmnt/* | xxh128digest)
[[ "$hash1" == "$hash2" ]] || log_fail "hash mismatch: $hash1 != $hash2"

log_pass "zfs can receive raw, recursive send streams"
