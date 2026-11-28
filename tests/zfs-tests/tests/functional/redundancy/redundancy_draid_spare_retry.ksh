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
# Copyright (c) 2026 by Wasabi Technologies, Inc.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#	When a dRAID child has been replaced by a distributed spare while the
#	original device is still attached, a read of a block whose distributed
#	spare copy is damaged must succeed, even if the block has more damage
#	than parity alone can repair.  The retried read compares the copies of
#	the spare vdev and uses the original device's copy.
#
# STRATEGY:
#	1. Create a draid1 pool where every redundancy group spans every disk,
#	   so every block has a column on every disk.
#	2. Write a file with known contents.
#	3. Disable the scrub which normally follows a sequential rebuild.
#	4. Inject read corruption on disk Q, offline disk X and sequentially
#	   rebuild X to the distributed spare.  With X unreadable the spare's
#	   columns are reconstructed from parity, and a sequential rebuild
#	   does not verify checksums, so the spare's copy of the columns
#	   rebuilt from corrupted reads of Q is silently wrong.  The data on
#	   disk on X and Q is intact since only reads were corrupted.
#	5. Clear the injection and online X.  X stays attached as the other
#	   child of the spare vdev; the resilver only covers the txgs X missed
#	   while offline, which don't include the file.  Export and import the
#	   pool so the file is read from disk.
#	6. Inject read corruption on Q again.  Blocks whose spare copy is
#	   wrong now have two damaged columns, which exceeds the single parity.
#	7. Read the file back and verify its contents.
#	8. Verify the vdev_mirror_stats kstats recorded retry compares which
#	   found differing copies.
#	9. Clear the injection and verify a scrub finds no errors.
#
#	Without the retry compare change step 7 fails with EIO.
#
#	zinject cannot target a distributed spare, which is why its copy is
#	damaged through the rebuild in step 4 instead.
#

verify_runnable "global"

typeset DSPARE="draid1-0-0"
typeset REF_FILE="$TEST_BASE_DIR/draid_spare_retry.ref"
typeset VDEV_PREFIX="$TEST_BASE_DIR/draid_spare_retry.vdev"
typeset VDEV_SIZE=512m
set -A DISKS

function cleanup
{
	zinject -c all >/dev/null 2>&1
	poolexists $TESTPOOL && destroy_pool $TESTPOOL
	rm -f $REF_FILE ${VDEV_PREFIX}.*
	log_must restore_tunable REBUILD_SCRUB_ENABLED
}

log_onexit cleanup

log_assert "A read succeeds using the original device's copy when the " \
    "distributed spare's copy is damaged beyond parity"

if [[ -z "$(kstat vdev_mirror_stats.retry_compare_mismatch)" ]]; then
	log_unsupported "Module lacks the retry_compare_mismatch kstat"
fi

# draid1:4d:6c:1s -> 5 active disks, group width 5, every group on every disk
typeset -i i=0
while (( i < 4 )); do
	DISKS[$i]="${VDEV_PREFIX}.$i"
	log_must truncate -s $VDEV_SIZE ${DISKS[$i]}
	(( i += 1 ))
done

log_must zpool create -f -O compression=off -O recordsize=128k \
    -O atime=off $TESTPOOL draid1:2d:4c:1s ${DISKS[@]}

# A larger file means more rebuild I/Os, and so more damaged spare columns.
log_must dd if=/dev/urandom of=$REF_FILE bs=1M count=64
log_must cp $REF_FILE /$TESTPOOL/file
log_must zpool sync $TESTPOOL

# The post-rebuild scrub would repair the spare's copy straight away.
log_must save_tunable REBUILD_SCRUB_ENABLED
log_must set_tunable32 REBUILD_SCRUB_ENABLED 0

#
# X = DISKS[1] is offlined and spared, Q = DISKS[2] returns corrupted reads.
# X must be unreadable during the rebuild: if it is readable, the rebuild
# copies X's good data to the spare instead of reconstructing it.
#
log_must zinject -d ${DISKS[2]} -e corrupt -T read -f 100 $TESTPOOL
log_must zpool offline $TESTPOOL ${DISKS[1]}
log_must zpool replace -s -w $TESTPOOL ${DISKS[1]} $DSPARE
log_must zinject -c all
log_must check_vdev_state $TESTPOOL $DSPARE "ONLINE"

#
# Bring X back; it only resilvers the txgs it missed while offline.  Use
# 'zpool online' rather than 'zpool clear' to keep the d-spare attached
# once the resilver completes.
#
log_must zpool online $TESTPOOL ${DISKS[1]}
log_must check_vdev_state $TESTPOOL ${DISKS[1]} "ONLINE"
sleep 2
log_must zpool wait -t resilver $TESTPOOL
log_must check_hotspare_state $TESTPOOL $DSPARE "INUSE"

#
# Drop cached data so the file is read from disk.  Nothing must read the
# file before the injection below: a normal read would repair the spare.
#
log_must zpool export $TESTPOOL
log_must zpool import -d $TEST_BASE_DIR $TESTPOOL
log_must zinject -d ${DISKS[2]} -e corrupt -T read -f 100 $TESTPOOL

typeset -i before_cmp=$(kstat vdev_mirror_stats.retry_compare)
typeset -i before_mis=$(kstat vdev_mirror_stats.retry_compare_mismatch)

log_must cmp $REF_FILE /$TESTPOOL/file

typeset -i after_cmp=$(kstat vdev_mirror_stats.retry_compare)
typeset -i after_mis=$(kstat vdev_mirror_stats.retry_compare_mismatch)
log_note "retry compares: $((after_cmp - before_cmp)), " \
    "mismatches: $((after_mis - before_mis))"

if (( after_mis <= before_mis )); then
	log_unsupported "No retry compare mismatches were recorded"
fi
log_must check_pool_status $TESTPOOL "errors" "No known data errors"

# The damage was injected on read only, so the on-disk data is intact.
log_must zinject -c all
log_must zpool clear $TESTPOOL
log_must zpool scrub -w $TESTPOOL
log_must check_pool_status $TESTPOOL "scan" "with 0 errors"
log_must check_pool_status $TESTPOOL "errors" "No known data errors"
log_must cmp $REF_FILE /$TESTPOOL/file

log_pass "A read succeeds using the original device's copy when the " \
    "distributed spare's copy is damaged beyond parity"
