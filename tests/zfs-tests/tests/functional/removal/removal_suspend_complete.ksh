#! /bin/ksh -p
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
# Copyright (c) 2026 by Will Rouesnel. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/removal/removal.kshlib

#
# DESCRIPTION:
#
# An export and "zpool remove -s" suspend the removal thread with
# spa_vdev_remove_suspend(), which waits on the removal without the
# namespace lock.  When the removal completes meanwhile, its last txg
# frees the removal state in spa_finish_removal().  Ensure that this
# doesn't happen while a woken suspend still has to use it.
#
# STRATEGY:
#
# 1. Create a pool of two file vdevs and write some data.
# 2. Arm zfs_removal_suspend_complete_race and remove the first vdev.
#    The removal thread then waits, once its copy is done, for a suspend
#    to start, and the suspend takes svr_lock back late after its wakeup
#    (2 seconds after spa_finish_removal() has done its own suspend).
# 3. Export the pool while the removal waits, and check that the export
#    succeeds and that the removal was freed only after the suspend was
#    done with it (stage 6; 7 means it was freed under the suspend).
# 4. Import the pool and check that the removal completed, that the vdev
#    is gone and that the data and the pool are intact.
# 5. Repeat with "zpool remove -s", which must fail because the removal
#    completed.
#

verify_runnable "global"

DISK1="$TEST_BASE_DIR/rmsuspend1"
DISK2="$TEST_BASE_DIR/rmsuspend2"

function cleanup
{
	log_must set_tunable32 REMOVAL_SUSPEND_COMPLETE_RACE 0
	poolexists $TESTPOOL1 || \
	    zpool import -d $TEST_BASE_DIR $TESTPOOL1 >/dev/null 2>&1
	poolexists $TESTPOOL1 && destroy_pool $TESTPOOL1
	log_must rm -f $DISK1 $DISK2
}
log_onexit cleanup

# Wait up to a minute for the race hook to reach a stage.
function wait_stage # stage
{
	typeset -i i=0

	while (( i < 600 )); do
		[[ $(get_tunable REMOVAL_SUSPEND_COMPLETE_RACE) == $1 ]] && \
		    return 0
		sleep 0.1
		(( i += 1 ))
	done
	log_note "stage $(get_tunable REMOVAL_SUSPEND_COMPLETE_RACE)"
	return 1
}

function check_stage
{
	typeset stage=$(get_tunable REMOVAL_SUSPEND_COMPLETE_RACE)

	case $stage in
	6)	log_note "The removal was freed after the suspend was done" ;;
	7)	log_fail "The removal was freed while a suspend still used it" ;;
	*)	log_fail "The suspend didn't wait across the completion" \
		    "(stage $stage)" ;;
	esac
}

function test_one # export|cancel
{
	log_must truncate -s $MINVDEVSIZE $DISK1 $DISK2
	log_must zpool create -f -O compression=off $TESTPOOL1 $DISK1 $DISK2
	log_must dd if=/dev/urandom of=/$TESTPOOL1/file bs=1M count=64
	typeset sum=$(xxh128digest /$TESTPOOL1/file)
	sync_pool $TESTPOOL1

	log_must set_tunable32 REMOVAL_SUSPEND_COMPLETE_RACE 1
	log_must zpool remove $TESTPOOL1 $DISK1
	log_must wait_stage 2

	if [[ $1 == export ]]; then
		log_must zpool export $TESTPOOL1
		check_stage
		log_must zpool import -d $TEST_BASE_DIR $TESTPOOL1
	else
		log_mustnot zpool remove -s $TESTPOOL1
		check_stage
	fi
	log_must set_tunable32 REMOVAL_SUSPEND_COMPLETE_RACE 0

	sync_pool $TESTPOOL1
	log_must is_pool_removed $TESTPOOL1
	log_mustnot vdevs_in_pool $TESTPOOL1 $DISK1
	log_must [ "$(xxh128digest /$TESTPOOL1/file)" = "$sum" ]
	log_must zpool scrub -w $TESTPOOL1
	log_must check_pool_status $TESTPOOL1 "errors" "No known data errors"
	log_must zdb -cd $TESTPOOL1
	log_must zpool destroy $TESTPOOL1
	log_must rm -f $DISK1 $DISK2
}

log_assert "A suspend that waits across a removal's completion is safe"

test_one export
test_one cancel

log_pass "A suspend that waits across a removal's completion is safe"
