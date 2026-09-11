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

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/replacement/replacement.cfg

#
# DESCRIPTION:
#	Verify that a resilver does not restart after completing while a pool
#	checkpoint is present.
#
# STRATEGY:
#	1. Create a single-disk pool and write data.
#	2. Start an attach resilver and suspend scan progress.
#	3. Create a checkpoint while the resilver is active.
#	4. Let the resilver finish.
#	5. Verify the checkpoint-retained DTL does not start another resilver.
#

verify_runnable "global"

function cleanup
{
	log_must set_tunable32 SCAN_SUSPEND_PROGRESS \
	    $ORIG_SCAN_SUSPEND_PROGRESS
	log_must set_tunable32 RESILVER_MIN_TIME_MS $ORIG_RESILVER_MIN_TIME
	log_must zpool events
	destroy_pool $TESTPOOL1
	rm -f ${VDEV_FILES[0]} $SPARE_VDEV_FILE
}

function wait_for_event # pattern timeout
{
	typeset pattern=$1
	typeset timeout=${2:-60}
	typeset -i events=0

	for (( iter = 0; iter < timeout; iter++ )); do
		events=$(zpool events | grep -cF "$pattern")
		(( events > 0 )) && return 0
		sleep 1
	done

	return 1
}

log_assert "Checkpointed DTLs do not restart a completed resilver"

ORIG_SCAN_SUSPEND_PROGRESS=$(get_tunable SCAN_SUSPEND_PROGRESS)
ORIG_RESILVER_MIN_TIME=$(get_tunable RESILVER_MIN_TIME_MS)

log_onexit cleanup

log_must truncate -s $VDEV_FILE_SIZE ${VDEV_FILES[0]} $SPARE_VDEV_FILE
log_must zpool create -f -O recordsize=128K $TESTPOOL1 ${VDEV_FILES[0]}
log_must eval "dd if=/dev/urandom of=/$TESTPOOL1/file bs=1M count=32" \
	">/dev/null 2>&1"

log_must zpool events -c
log_must set_tunable32 RESILVER_MIN_TIME_MS 20
log_must set_tunable32 SCAN_SUSPEND_PROGRESS 1
log_must zpool attach $TESTPOOL1 ${VDEV_FILES[0]} $SPARE_VDEV_FILE

log_must wait_for_event "sysevent.fs.zfs.resilver_start" 60
log_must zpool checkpoint $TESTPOOL1

log_must set_tunable32 SCAN_SUSPEND_PROGRESS 0
log_must wait_for_event "sysevent.fs.zfs.resilver_finish" 120

# Wait a few txgs to ensure completion does not queue a deferred resilver.
sync_pool $TESTPOOL1 true
sync_pool $TESTPOOL1 true

resilver_starts=$(zpool events | grep -cF "sysevent.fs.zfs.resilver_start")
(( resilver_starts != 1 )) &&
	log_fail "expected 1 resilver start, found $resilver_starts"

log_pass "Completed resilver was not restarted with checkpoint present"
