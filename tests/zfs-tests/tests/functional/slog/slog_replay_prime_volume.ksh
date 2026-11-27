#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# https://opensource.org/license/CDDL-1.0.
#

#
# Copyright (c) 2026, Amazon.com, Inc. or its affiliates.
#

. $STF_SUITE/tests/functional/slog/slog.kshlib

#
# DESCRIPTION:
#	Verify ZIL replay with ARC priming is correct for a volume.
#
# STRATEGY:
#	1. Create a volume with a file system on it, and fill files before
#	   freezing the pool, so replay must read the blocks it partially
#	   writes.
#	2. Freeze the pool and partially overwrite the files synchronously.
#	   On Linux, also remove a file and discard its blocks (TX_TRUNCATE).
#	3. Checksum the files, unmount and export the pool.
#	4. Import with priming enabled, verify priming ran and the
#	   checksums match.
#

verify_runnable "global"

typeset VOLUME=$ZVOL_DEVDIR/$TESTPOOL/$TESTVOL
typeset MNTPNT=$TESTDIR/$TESTVOL
typeset FSTYPE=none
typeset -i prime_saved=0

function cleanup_prime_volume
{
	if ismounted $MNTPNT $FSTYPE; then
		log_must umount $MNTPNT
		rmdir $MNTPNT
	fi
	(( prime_saved != 0 )) && restore_tunable ZIL_REPLAY_PRIME_ARC
	cleanup
}

log_assert "ZIL replay of a volume with ARC priming succeeds"
log_onexit cleanup_prime_volume

log_must setup
log_must save_tunable ZIL_REPLAY_PRIME_ARC
prime_saved=1

#
# 1. Create the volume and file system, and fill the files.
#
log_must zpool create $TESTPOOL $VDEV log $SDEV
log_must zfs create -V 128M -o volblocksize=16k -o compression=off \
    $TESTPOOL/$TESTVOL
log_must zfs set sync=always $TESTPOOL/$TESTVOL
log_must mkdir -p $MNTPNT
block_device_wait
if is_linux; then
	log_must new_fs -t ext4 -v $VOLUME
	log_must mount -o discard $VOLUME $MNTPNT
	FSTYPE=ext4
	log_must rmdir $MNTPNT/lost+found
else
	log_must new_fs $VOLUME
	log_must mount $VOLUME $MNTPNT
	FSTYPE=$NEWFS_DEFAULT_FS
fi

typeset -i i
for i in $(seq 1 16); do
	log_must dd if=/dev/urandom of=$MNTPNT/f.$i bs=128k count=4 status=none
done
sync_all_pools

#
# 2. Freeze the pool and partially overwrite the files.
#
log_must zpool freeze $TESTPOOL
for i in $(seq 1 16); do
	log_must dd if=/dev/urandom of=$MNTPNT/f.$i bs=4k count=1 \
	    seek=$((i * 3 + 1)) conv=notrunc oflag=sync status=none
	log_must dd if=/dev/urandom of=$MNTPNT/f.$i bs=8k count=3 \
	    seek=$((i + 20)) conv=notrunc oflag=sync status=none
done
if is_linux; then
	# With -o discard, ext4 discards the blocks when it commits.
	log_must rm $MNTPNT/f.16
	log_must sync
fi

#
# 3. Checksum the files, unmount and export.
#
typeset checksum=$(cat $MNTPNT/f.* | xxh128digest)
[[ -n "$checksum" ]] || log_fail "could not checksum the files"
log_must umount $MNTPNT
log_must_busy zpool export $TESTPOOL

#
# 4. Replay with priming and verify.
#
log_must set_tunable32 ZIL_REPLAY_PRIME_ARC 1
typeset -i before=$(prime_arc_count)
log_must zpool import -f -d $VDIR $TESTPOOL
block_device_wait
typeset -i after=$(prime_arc_count)
(( after > before )) || log_fail "ARC priming did not run during replay"

log_must mount $VOLUME $MNTPNT
typeset checksum1=$(cat $MNTPNT/f.* | xxh128digest)
[[ "$checksum1" == "$checksum" ]] || \
    log_fail "checksum mismatch ($checksum1 != $checksum)"

log_pass "ZIL replay of a volume with ARC priming succeeds"
