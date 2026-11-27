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
#	Verify ZIL replay with ARC priming is correct when the blocks it
#	prefetches are damaged on one side of a mirror.
#
# STRATEGY:
#	1. Create files on a mirror with a separate log device.
#	2. Freeze the pool and log partial writes, links and setattrs.
#	3. Copy the file system and export the pool.
#	4. Overwrite the data area of one mirror side with random data.
#	5. Import with priming enabled.
#	6. Verify the file system matches the copy, checksum errors were
#	   found on the damaged side, and there are no data errors.
#	7. Scrub and verify the pool has no data errors.
#

verify_runnable "global"

typeset FS=/$TESTPOOL/$TESTFS
typeset LEG_A=$VDIR/a
typeset LEG_B=$VDIR/b
typeset -i prime_saved=0

function cleanup_prime_heal
{
	(( prime_saved != 0 )) && restore_tunable ZIL_REPLAY_PRIME_ARC
	cleanup
}

log_assert "ZIL replay with ARC priming heals from a damaged mirror side"
log_onexit cleanup_prime_heal

log_must setup
log_must save_tunable ZIL_REPLAY_PRIME_ARC
prime_saved=1

log_must zpool create $TESTPOOL mirror $LEG_A $LEG_B log $SDEV
log_must zfs create -o recordsize=128k -o compression=off $TESTPOOL/$TESTFS

#
# 1. Create the objects that replay will modify.
#
typeset -i i
for i in $(seq 1 32); do
	log_must dd if=/dev/urandom of=$FS/f.$i bs=128k count=8 status=none
	log_must touch $FS/l.$i
done
sync_all_pools
log_must dd if=/dev/zero of=$FS/sync conv=fdatasync,fsync bs=1 count=1

#
# 2. Freeze the pool and log the records.
#
log_must zpool freeze $TESTPOOL
for i in $(seq 1 32); do
	log_must dd if=/dev/urandom of=$FS/f.$i bs=100000 count=2 seek=1 \
	    conv=notrunc oflag=sync status=none
	log_must ln $FS/l.$i $FS/l.$i.link
	log_must chmod 640 $FS/l.$i
done

#
# 3. Copy the file system and export the pool.
#
log_must mkdir -p $TESTDIR
log_must rsync -aHAX $FS/ $TESTDIR/copy
log_must zfs unmount $FS
log_must zpool export $TESTPOOL

#
# 4. Damage everything on one mirror side but its labels, which are in the
#    first and last 512 KiB.
#
typeset -i size_mb=$((MINVDEVSIZE / 1024 / 1024))
log_must dd if=/dev/urandom of=$LEG_B bs=1M seek=4 count=$((size_mb - 8)) \
    conv=notrunc status=none

#
# 5. Replay with priming.
#
log_must set_tunable32 ZIL_REPLAY_PRIME_ARC 1
typeset -i before=$(prime_arc_count)
log_must zpool import -f -d $VDIR $TESTPOOL
typeset -i after=$(prime_arc_count)
(( after > before )) || log_fail "ARC priming did not run during replay"

#
# 6. Verify the replayed file system and the error counts.
#
log_must replay_directory_diff $TESTDIR/copy $FS
typeset -i cksum=$(zpool status -p $TESTPOOL | \
    awk -v leg=$LEG_B '$1 == leg { print $5 }')
(( cksum > 0 )) || log_fail "no checksum errors on the damaged side"
log_must eval "zpool status -v $TESTPOOL | grep -q 'No known data errors'"

#
# 7. Scrub and verify.
#
log_must zpool scrub -w $TESTPOOL
log_must eval "zpool status -v $TESTPOOL | grep -q 'No known data errors'"
log_must replay_directory_diff $TESTDIR/copy $FS

log_pass "ZIL replay with ARC priming heals from a damaged mirror side"
