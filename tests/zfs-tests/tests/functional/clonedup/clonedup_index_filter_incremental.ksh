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
#	A default run after a finished one matches its new blocks
#	against older data.  The counting pre-pass counts only the
#	new blocks, so that run never takes it, even with the
#	pre-pass forced on.  A quick run pairs new blocks among
#	themselves and still takes it.
#
# STRATEGY:
#	1. Finish a first run with the pre-pass off, so the pool has
#	   a last_clonedup_txg.
#	2. Copy an old file.  Each new block then has exactly one
#	   duplicate, and that duplicate is older than the window.
#	3. Force the pre-pass on and make a default run.
#	4. The copy shares every block with the original, and the
#	   run took no pre-pass and dropped nothing.
#	5. Write a new pair and a unique file and make a quick run.
#	   It takes the pre-pass, drops the unique blocks and still
#	   clones the pair.
#

verify_runnable "global"

function cleanup
{
	log_must restore_tunable CLONEDUP_INDEX_FILTER
	clonedup_cleanup
}

log_assert "a run that matches older data takes no counting pre-pass"
log_onexit cleanup
log_must save_tunable CLONEDUP_INDEX_FILTER

log_must set_tunable32 CLONEDUP_INDEX_FILTER 0
clonedup_pool_create
clonedup_write /$TESTPOOL/a
clonedup_sync
clonedup_run
typeset txg1=$(clonedup_last_txg)
log_must [ $txg1 -gt 0 ]
clonedup_kstat_is count_walks 0

clonedup_dup /$TESTPOOL/a /$TESTPOOL/b
clonedup_sync

log_must set_tunable32 CLONEDUP_INDEX_FILTER 1
clonedup_run
clonedup_check_shared $TESTPOOL a $TESTPOOL b "$(clonedup_all_blocks)"
clonedup_jstat_is clonedup_indexed $CD_BLOCKS
clonedup_jstat_is clonedup_applied $CD_BLOCKS
clonedup_kstat_is count_walks 0
clonedup_kstat_is filter_dropped 0
log_must [ $(clonedup_last_txg) -gt $txg1 ]
log_must cmp /$TESTPOOL/a /$TESTPOOL/b

clonedup_write /$TESTPOOL/c
clonedup_dup /$TESTPOOL/c /$TESTPOOL/d
clonedup_write /$TESTPOOL/e
clonedup_sync

clonedup_run -q
clonedup_check_shared $TESTPOOL c $TESTPOOL d "$(clonedup_all_blocks)"
clonedup_kstat_is count_walks 1
clonedup_kstat_gt filter_dropped 0
clonedup_jstat_is errors 0
log_must cmp /$TESTPOOL/c /$TESTPOOL/d
clonedup_leakcheck

log_pass "a run that matches older data takes no counting pre-pass"
