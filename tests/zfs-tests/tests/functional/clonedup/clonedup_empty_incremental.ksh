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
#	The matching walk over old data only runs when the new data
#	indexed something.  The first run has no old data, a run after
#	new writes walks once, and a run with nothing new does not.
#

verify_runnable "global"

log_assert "an incremental run with nothing new skips the match walk"
log_onexit clonedup_cleanup

clonedup_pool_create
typeset all="$(clonedup_all_blocks)"

clonedup_write /$TESTPOOL/a
clonedup_dup /$TESTPOOL/a /$TESTPOOL/b
clonedup_sync
clonedup_run
clonedup_check_shared $TESTPOOL a $TESTPOOL b "$all"
clonedup_kstat_is index_walks 1
clonedup_kstat_is match_walks 0

clonedup_dup /$TESTPOOL/a /$TESTPOOL/c
clonedup_sync
clonedup_run
clonedup_check_shared $TESTPOOL a $TESTPOOL c "$all"
clonedup_kstat_is index_walks 2
clonedup_kstat_is match_walks 1

typeset examined=$(clonedup_jstat examined)
clonedup_assert_counts "$examined"
clonedup_run
clonedup_kstat_is index_walks 3
clonedup_kstat_is match_walks 1
clonedup_jstat_is clonedup_indexed 0
clonedup_jstat_is clonedup_candidates 0
typeset now=$(clonedup_jstat examined)
clonedup_assert_counts "$now"
log_must [ $now -lt $examined ]
clonedup_leakcheck

log_pass "an incremental run with nothing new skips the match walk"
