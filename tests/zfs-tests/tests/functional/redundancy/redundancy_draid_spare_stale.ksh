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
#	During a healing resilver of a device which is attached under a
#	distributed spare, the verified repairs which only need to reach the
#	children lacking a txg must reach the stale device.  The distributed
#	spare already holds the data and is not rewritten.
#
# STRATEGY:
#	1. Create a draid1 pool and write a file.
#	2. Offline disk X and sequentially rebuild it to the distributed
#	   spare.  X stays attached as the other child of the spare vdev.
#	3. Write a second file while X is offline, so X genuinely lacks it.
#	4. Online X and let the healing resilver run.  The distributed spare
#	   is current for the new txgs, so the resilver repairs are written
#	   to X only.
#	5. Detach the distributed spare, so X alone serves its column.
#	6. Verify a scrub repairs nothing, X has no checksum errors and both
#	   files read back intact.  If the resilver repairs did not reach X,
#	   the scrub would find X's copy of the second file wrong.
#

verify_runnable "global"

typeset DSPARE="draid1-0-0"
typeset REF_FILE1="$TEST_BASE_DIR/draid_spare_stale.ref1"
typeset REF_FILE2="$TEST_BASE_DIR/draid_spare_stale.ref2"
typeset VDEV_PREFIX="$TEST_BASE_DIR/draid_spare_stale.vdev"
typeset VDEV_SIZE=512m
set -A DISKS

function cleanup
{
	poolexists $TESTPOOL && destroy_pool $TESTPOOL
	rm -f $REF_FILE1 $REF_FILE2 ${VDEV_PREFIX}.*
}

#
# Print the cumulative write operations of the distributed spare.
#
function dspare_writes
{
	zpool iostat -vp $TESTPOOL | awk -v d="$DSPARE" '$1 == d { print $5 }'
}

log_onexit cleanup

log_assert "A dRAID healing resilver under a distributed spare writes the " \
    "verified repairs to the stale device"

typeset -i i=0
while (( i < 6 )); do
	DISKS[$i]="${VDEV_PREFIX}.$i"
	log_must truncate -s $VDEV_SIZE ${DISKS[$i]}
	(( i += 1 ))
done

log_must zpool create -f -O compression=off -O atime=off \
    $TESTPOOL draid1:4d:6c:1s ${DISKS[@]}

log_must dd if=/dev/urandom of=$REF_FILE1 bs=1M count=32
log_must dd if=/dev/urandom of=$REF_FILE2 bs=1M count=32
log_must cp $REF_FILE1 /$TESTPOOL/file1
log_must zpool sync $TESTPOOL

# X = DISKS[1] is offlined and rebuilt to the distributed spare.
log_must zpool offline $TESTPOOL ${DISKS[1]}
log_must zpool replace -s -w $TESTPOOL ${DISKS[1]} $DSPARE
log_must zpool wait -t scrub $TESTPOOL
log_must check_vdev_state $TESTPOOL $DSPARE "ONLINE"

# X genuinely lacks the second file.
log_must cp $REF_FILE2 /$TESTPOOL/file2
log_must zpool sync $TESTPOOL

#
# Healing resilver of the txgs X missed.  'zpool online' rather than
# 'zpool clear' keeps the distributed spare attached afterwards.
#
typeset before=$(dspare_writes)
log_must zpool online $TESTPOOL ${DISKS[1]}
log_must check_vdev_state $TESTPOOL ${DISKS[1]} "ONLINE"
sleep 2
log_must zpool wait -t resilver $TESTPOOL
typeset after=$(dspare_writes)
log_note "distributed spare writes during resilver: $before -> $after"
log_must check_pool_status $TESTPOOL "scan" "with 0 errors"
log_must check_hotspare_state $TESTPOOL $DSPARE "INUSE"

# X alone serves its column from now on.
log_must zpool detach $TESTPOOL $DSPARE
log_must check_vdev_state $TESTPOOL ${DISKS[1]} "ONLINE"

log_must zpool export $TESTPOOL
log_must zpool import -d $TEST_BASE_DIR $TESTPOOL
log_must zpool scrub -w $TESTPOOL
log_must check_pool_status $TESTPOOL "scan" "repaired 0B"
log_must check_pool_status $TESTPOOL "scan" "with 0 errors"
log_must eval "zpool status -p $TESTPOOL | awk -v d='${DISKS[1]}' \
    '\$1 == d && \$5 != 0 { exit 1 }'"
log_must cmp $REF_FILE1 /$TESTPOOL/file1
log_must cmp $REF_FILE2 /$TESTPOOL/file2

log_pass "A dRAID healing resilver under a distributed spare writes the " \
    "verified repairs to the stale device"
