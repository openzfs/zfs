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
#	A run with several apply workers can be stopped while every
#	worker is inside the apply, the pool exported and imported,
#	and a later run finishes the work.  Each step must return
#	within a bound: a worker that never leaves its loop keeps the
#	apply thread waiting for it, and an export waits for the
#	apply thread.
#

verify_runnable "global"

typeset -i nblk=16
typeset -i hold_ms=1000
typeset -i bound=120

function cleanup
{
	log_must restore_tunable CLONEDUP_APPLY_TXG_DELAY
	log_must restore_tunable CLONEDUP_APPLY_THREADS
	clonedup_cleanup
}

log_assert "a run with several workers stops and survives an export"
log_onexit cleanup
log_must save_tunable CLONEDUP_APPLY_TXG_DELAY
log_must save_tunable CLONEDUP_APPLY_THREADS

clonedup_pool_create
clonedup_write /$TESTPOOL/a $nblk
clonedup_dup /$TESTPOOL/a /$TESTPOOL/b
clonedup_sync

# Each candidate takes hold_ms, so the stop lands while the workers
# are in the middle of the apply.
log_must set_tunable32 CLONEDUP_APPLY_THREADS 2
log_must set_tunable32 CLONEDUP_APPLY_TXG_DELAY $hold_ms

log_must zpool clonedup $TESTPOOL
clonedup_wait_applying
clonedup_wait_apply_done 2
clonedup_bounded $bound zpool clonedup -s $TESTPOOL
clonedup_bounded $bound zpool wait -t clonedup $TESTPOOL
clonedup_jstat_is state CANCELED

clonedup_bounded $bound zpool export $TESTPOOL
clonedup_bounded $bound zpool import $TESTPOOL
clonedup_jstat_is state CANCELED

clonedup_bounded $bound zpool clonedup -w $TESTPOOL
clonedup_jstat_is state FINISHED
clonedup_jstat_is errors 0
clonedup_check_shared $TESTPOOL a $TESTPOOL b \
    "$(clonedup_all_blocks $nblk)"
log_must cmp /$TESTPOOL/a /$TESTPOOL/b
clonedup_leakcheck

log_pass "a run with several workers stops and survives an export"
