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
#	A scrub request cancels a running clonedup without advancing
#	last_clonedup_txg; a clonedup request during a scrub is
#	refused.
#

verify_runnable "global"

log_assert "scrubs preempt clonedup, clonedup never preempts a scrub"
log_onexit clonedup_cleanup

clonedup_pool_create
clonedup_write /$TESTPOOL/a
clonedup_dup /$TESTPOOL/a /$TESTPOOL/b
clonedup_sync

clonedup_hold
log_must zpool clonedup $TESTPOOL
clonedup_wait_running
log_must zpool scrub $TESTPOOL
log_must is_pool_scrubbing $TESTPOOL
clonedup_kstat_is canceled_runs 1
log_must [ $(clonedup_last_txg) -eq 0 ]

log_mustnot zpool clonedup $TESTPOOL
log_mustnot zpool clonedup -f $TESTPOOL
log_must is_pool_scrubbing $TESTPOOL
log_must zpool scrub -s $TESTPOOL
clonedup_release

clonedup_run
clonedup_check_shared $TESTPOOL a $TESTPOOL b "$(clonedup_all_blocks)"
log_must zpool scrub -w $TESTPOOL
log_must eval "zpool status $TESTPOOL | grep -q 'scrub repaired 0B'"
clonedup_leakcheck

log_pass "scrubs preempt clonedup, clonedup never preempts a scrub"
