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
#	A second run indexes only blocks written since the first one
#	and still matches them against the older data.  A run with
#	nothing new indexes nothing.
#

verify_runnable "global"

log_assert "incremental runs index new blocks and match old ones"
log_onexit clonedup_cleanup

clonedup_pool_create
clonedup_write /$TESTPOOL/a
clonedup_sync
clonedup_run
typeset txg1=$(clonedup_last_txg)
clonedup_assert_counts "$txg1"
clonedup_jstat_is clonedup_applied 0

clonedup_dup /$TESTPOOL/a /$TESTPOOL/c
clonedup_sync
clonedup_run
clonedup_jstat_is clonedup_indexed $CD_BLOCKS
clonedup_jstat_is clonedup_groups $CD_BLOCKS
clonedup_jstat_is clonedup_candidates $CD_BLOCKS
clonedup_jstat_is clonedup_applied $CD_BLOCKS
clonedup_check_shared $TESTPOOL a $TESTPOOL c "$(clonedup_all_blocks)"
typeset txg2=$(clonedup_last_txg)
clonedup_assert_counts "$txg2"
log_must [ $txg2 -gt $txg1 ]

clonedup_run
clonedup_jstat_is clonedup_indexed 0
clonedup_jstat_is clonedup_applied 0
log_must eval \
    "zpool status $TESTPOOL | grep -q 'completed: 0 blocks cloned'"
clonedup_leakcheck

log_pass "incremental runs index new blocks and match old ones"
