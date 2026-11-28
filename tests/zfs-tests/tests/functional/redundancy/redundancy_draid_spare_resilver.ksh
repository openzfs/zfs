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
. $STF_SUITE/tests/functional/redundancy/redundancy.kshlib

#
# DESCRIPTION:
#	A healing resilver of a device which is still attached under a
#	distributed spare must not overwrite that device's good data with an
#	unverified, damaged copy from the distributed spare, and must recover
#	blocks which can only be reconstructed with the device's copy.
#
#	Failed scrub repair writes to a faulted device put the birth txgs of
#	the repaired blocks into its DTL.  DTLs track txgs, not blocks, so the
#	device is then considered stale for every block of those txgs, even
#	though it still holds good copies of the blocks which could not be
#	repaired.
#
# STRATEGY:
#	1. Create a draid1 pool where every redundancy group spans every disk.
#	2. Write a file with known contents.
#	3. Disable the scrub which normally follows a sequential rebuild.
#	4. Inject read corruption on disk Q, fault disk X and sequentially
#	   rebuild X to the distributed spare.  The spare's copy of the columns
#	   rebuilt from corrupted reads of Q is silently wrong.
#	5. Scrub with Q still corrupted.  Blocks with two damaged columns
#	   cannot be repaired, and the repair writes to the faulted X fail,
#	   which puts the file's txgs into X's DTL.
#	6. Clear X and let the healing resilver run, Q still corrupted.
#	7. Verify the resilver recovered the damaged blocks by comparing the
#	   copies under the spare vdev (retry_compare_mismatch increased) and
#	   finished without errors.
#	8. Clear the injection, offline disk Q, verify the file contents,
#	   and verify a scrub finds no errors and nothing to repair.
#
#	Without the fix the resilver copies the spare's damaged data onto X
#	before it is verified, finishes with errors, and the file is lost.
#

verify_runnable "global"

typeset DSPARE="draid1-0-0"
typeset REF_FILE="$TEST_BASE_DIR/draid_spare_resilver.ref"
typeset VDEV_PREFIX="$TEST_BASE_DIR/draid_spare_resilver.vdev"
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

log_assert "A resilver under a distributed spare recovers blocks using " \
    "the original device's copy and does not destroy it"

if [[ -z "$(kstat vdev_mirror_stats.retry_compare_mismatch)" ]]; then
	log_unsupported "Module lacks the retry_compare_mismatch kstat"
fi

typeset -i i=0
while (( i < 4 )); do
	DISKS[$i]="${VDEV_PREFIX}.$i"
	log_must truncate -s $VDEV_SIZE ${DISKS[$i]}
	(( i += 1 ))
done

log_must zpool create -f -O compression=off -O recordsize=128k \
    -O atime=off $TESTPOOL draid1:2d:4c:1s ${DISKS[@]}

log_must dd if=/dev/urandom of=$REF_FILE bs=1M count=64
log_must cp $REF_FILE /$TESTPOOL/file
log_must zpool sync $TESTPOOL

log_must save_tunable REBUILD_SCRUB_ENABLED
log_must set_tunable32 REBUILD_SCRUB_ENABLED 0

# X = DISKS[1] is faulted and spared, Q = DISKS[2] returns corrupted reads.
log_must zinject -d ${DISKS[2]} -e corrupt -T read -f 100 $TESTPOOL
log_must zpool offline -f $TESTPOOL ${DISKS[1]}
log_must zpool replace -s -w $TESTPOOL ${DISKS[1]} $DSPARE
log_must check_vdev_state $TESTPOOL $DSPARE "ONLINE"

# Expected to find unrecoverable blocks; X's DTL gains the file's txgs.
log_must zpool scrub -w $TESTPOOL

typeset -i before_mis=$(kstat vdev_mirror_stats.retry_compare_mismatch)

# Bring X back and let the healing resilver run, Q still corrupted.
log_must zpool clear $TESTPOOL ${DISKS[1]}
log_must check_vdev_state $TESTPOOL ${DISKS[1]} "ONLINE"
sleep 2
log_must zpool wait -t resilver $TESTPOOL

typeset -i after_mis=$(kstat vdev_mirror_stats.retry_compare_mismatch)
log_note "retry compare mismatches during resilver: " \
    "$((after_mis - before_mis))"
log_must check_pool_status $TESTPOOL "scan" "with 0 errors"

# It can happen sometimes when file's data on dRAID spare under
# disk X turns out not to be corrupted. In this case the test is not
# actually testing anything and can be skipped. That's why we have it
# added to the the maybe list at zts-report.py.in.
if (( after_mis <= before_mis )); then
	log_unsupported "No retry compare mismatches were recorded during resilver"
fi

log_must zinject -c all
log_must zpool export $TESTPOOL
log_must zpool import -d $TEST_BASE_DIR $TESTPOOL
# Offline disk Q to make sure disk X has good data.
log_must zpool offline $TESTPOOL ${DISKS[2]}
log_must cmp $REF_FILE /$TESTPOOL/file

log_must zpool clear $TESTPOOL
log_must verify_draid_pool $TESTPOOL

log_pass "A resilver under a distributed spare recovers blocks using " \
    "the original device's copy and does not destroy it"
