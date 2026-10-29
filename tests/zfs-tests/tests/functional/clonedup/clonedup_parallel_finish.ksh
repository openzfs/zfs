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
#	With several apply workers, a run is not finished until every
#	worker has committed the clones it queued.  Two workers share
#	three candidates in one mounted dataset, so one of them runs
#	out of work while the other still holds the last candidate.
#	When "zpool clonedup -w" returns, every block must already be
#	shared and the run's record must count every clone the kstat
#	counted.
#

verify_runnable "global"

typeset -i nblk=3
typeset -i hold_ms=1000
typeset -i flush_ms=30000

function cleanup
{
	log_must restore_tunable CLONEDUP_APPLY_FLUSH_DELAY
	log_must restore_tunable CLONEDUP_APPLY_TXG_DELAY
	log_must restore_tunable CLONEDUP_APPLY_THREADS
	clonedup_cleanup
}

log_assert "a run with several workers ends after their last commits"
log_onexit cleanup
log_must save_tunable CLONEDUP_APPLY_FLUSH_DELAY
log_must save_tunable CLONEDUP_APPLY_TXG_DELAY
log_must save_tunable CLONEDUP_APPLY_THREADS

clonedup_pool_create
clonedup_write /$TESTPOOL/a $nblk
clonedup_dup /$TESTPOOL/a /$TESTPOOL/b
clonedup_sync

# Each candidate takes hold_ms, so both workers are busy from the
# start.  A worker that leaves its loop with clones queued waits
# flush_ms before committing them, far longer than the rest of the
# run takes.
log_must set_tunable32 CLONEDUP_APPLY_THREADS 2
log_must set_tunable32 CLONEDUP_APPLY_TXG_DELAY $hold_ms
log_must set_tunable32 CLONEDUP_APPLY_FLUSH_DELAY $flush_ms

typeset -i before=$(clonedup_kstat clones)
typeset -F t0=$SECONDS
clonedup_run
typeset -F took=$((SECONDS - t0))

# Read everything before a worker still asleep could commit.
typeset -i clones=$(( $(clonedup_kstat clones) - before ))
typeset applied=$(clonedup_jstat clonedup_applied)
typeset shared=$(clonedup_shared $TESTPOOL a $TESTPOOL b)
typeset -i nshared=$(echo $shared | wc -w)

log_note "kstat clones $clones, applied $applied, shared '$shared'"
log_note "the run took $took s, the flush delay is $flush_ms ms"
log_must [ $clones -eq $nblk ]
log_must [ "$applied" -eq $nblk ]
log_must [ $nshared -eq $applied ]
log_must [ "$shared" = "$(clonedup_all_blocks $nblk)" ]
clonedup_jstat_is state FINISHED
clonedup_jstat_is clonedup_applied $clones
clonedup_jstat_is errors 0
log_must cmp /$TESTPOOL/a /$TESTPOOL/b
clonedup_leakcheck

log_pass "a run with several workers ends after their last commits"
