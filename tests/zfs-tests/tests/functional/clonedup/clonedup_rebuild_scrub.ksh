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
# Copyright (c) 2026, Patrick Domack. All rights reserved.
#

. $STF_SUITE/tests/functional/clonedup/clonedup.kshlib

#
# DESCRIPTION:
#	The scrub that follows a sequential rebuild verifies the
#	checksums the rebuild did not.  It replaces a clonedup run in
#	progress, as a scrub requested from the command line does,
#	instead of being skipped because a scan is already running.
#	zpool status recommends that scrub until one ends after the
#	rebuild, and recommends none when the scan record holds a
#	canceled scrub or a resilver.  A clonedup run takes over the
#	record and does not change the recommendation.
#
# STRATEGY:
#	1. Hold a run in its apply with a delay on each candidate.
#	   zfs_scan_suspend_progress would hold the rebuild as well.
#	2. Attach a disk with a sequential rebuild and wait for it.
#	3. The scrub the rebuild started cancelled the run, and the
#	   scrub completes.
#	4. Turn that scrub off, detach the disk and attach it again
#	   with a sequential rebuild, then run clonedup.  zpool status
#	   must still recommend a scrub.
#	5. Scrub, then run clonedup.  The run's record keeps the
#	   scrub's end time, and zpool status recommends nothing, also
#	   after an export and import and after another run.
#	6. Cancel a scrub, then rebuild the disk again.  zpool status
#	   recommends nothing, before a clonedup run and after it.
#	7. Resilver the disk, then rebuild it again.  zpool status
#	   recommends nothing, before a clonedup run and after it.
#

verify_runnable "global"

function cleanup
{
	log_must restore_tunable CLONEDUP_APPLY_TXG_DELAY
	log_must restore_tunable CLONEDUP_APPLY_THREADS
	log_must restore_tunable REBUILD_SCRUB_ENABLED
	clonedup_cleanup
}

# The status message zpool status -j gives the pool, or nothing.
function pool_status
{
	zpool status -j $TESTPOOL |
	    jq -r --arg p $TESTPOOL '.pools[$p].status // empty'
}

# The function and the state of the pool's scan record.
function scan_func_state
{
	zpool status -j $TESTPOOL | jq -r --arg p $TESTPOOL \
	    '.pools[$p].scan_stats | "\(.function) \(.state)"'
}

log_assert "the scrub after a sequential rebuild replaces a" \
    "clonedup run"
log_onexit cleanup
log_must save_tunable REBUILD_SCRUB_ENABLED
log_must save_tunable CLONEDUP_APPLY_TXG_DELAY
log_must save_tunable CLONEDUP_APPLY_THREADS
log_must set_tunable32 REBUILD_SCRUB_ENABLED 1

typeset -a disks=($(clonedup_disks))
(( ${#disks[@]} >= 2 )) || log_unsupported "needs two disks"

log_must zpool create -f -o feature@clonedup=enabled \
    -O compression=off $TESTPOOL ${disks[0]}
clonedup_write /$TESTPOOL/a
clonedup_dup /$TESTPOOL/a /$TESTPOOL/b
clonedup_sync

# One worker and five seconds a candidate keep the run applying
# for far longer than rebuilding a few blocks takes.
log_must set_tunable32 CLONEDUP_APPLY_THREADS 1
log_must set_tunable32 CLONEDUP_APPLY_TXG_DELAY 5000
log_must zpool clonedup $TESTPOOL
clonedup_wait_applying

log_must zpool attach -s $TESTPOOL ${disks[0]} ${disks[1]}
log_must zpool wait -t resilver $TESTPOOL
log_must set_tunable32 CLONEDUP_APPLY_TXG_DELAY 0

clonedup_kstat_is canceled_runs 1
log_must zpool wait -t scrub $TESTPOOL
log_must eval "zpool status $TESTPOOL | grep -q 'scrub repaired'"

# Both end times count whole seconds, and the rebuild has to end
# after the scrub to call for another one.
sleep 1
log_must set_tunable32 REBUILD_SCRUB_ENABLED 0
log_must zpool detach $TESTPOOL ${disks[1]}
log_must zpool attach -s $TESTPOOL ${disks[0]} ${disks[1]}
log_must zpool wait -t resilver $TESTPOOL
clonedup_run
log_must eval "zpool status $TESTPOOL |" \
    "grep -q 'sequentially resilvered'"

# A scrub after the rebuild satisfies status, and a clonedup run
# keeps that scrub on record for it.
log_must zpool scrub -w $TESTPOOL
typeset scrub_end=$(zpool status -j --json-int $TESTPOOL |
    jq -r --arg p $TESTPOOL '.pools[$p].scan_stats.end_time')
clonedup_assert_counts "$scrub_end"
log_must [ -z "$(pool_status)" ]
clonedup_run
clonedup_record_is scrub_end $scrub_end
log_must [ -z "$(pool_status)" ]
log_must zpool export $TESTPOOL
log_must zpool import $TESTPOOL
log_must [ -z "$(pool_status)" ]
clonedup_run
log_must [ -z "$(pool_status)" ]

# A canceled scrub calls for no scrub after a rebuild, and a
# resilver does not either.  A run that takes over such a record
# must not call for one.  The sleep puts the rebuild's end a second
# past the cancel, as above.
clonedup_hold
log_must zpool scrub $TESTPOOL
log_must zpool scrub -s $TESTPOOL
clonedup_release
sleep 1
log_must zpool detach $TESTPOOL ${disks[1]}
log_must zpool attach -s $TESTPOOL ${disks[0]} ${disks[1]}
log_must zpool wait -t resilver $TESTPOOL
log_must [ "$(scan_func_state)" = "SCRUB CANCELED" ]
log_must [ -z "$(pool_status)" ]
clonedup_run
log_must [ -z "$(pool_status)" ]

log_must zpool detach $TESTPOOL ${disks[1]}
log_must zpool attach $TESTPOOL ${disks[0]} ${disks[1]}
log_must zpool wait -t resilver $TESTPOOL
log_must zpool detach $TESTPOOL ${disks[1]}
log_must zpool attach -s $TESTPOOL ${disks[0]} ${disks[1]}
log_must zpool wait -t resilver $TESTPOOL
log_must [ "$(scan_func_state)" = "RESILVER FINISHED" ]
log_must [ -z "$(pool_status)" ]
clonedup_run
log_must [ -z "$(pool_status)" ]
clonedup_leakcheck

log_pass "the scrub after a sequential rebuild replaces a" \
    "clonedup run"
