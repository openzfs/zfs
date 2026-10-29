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
#	Clones go out in batched transactions.  One transaction per
#	block would mean one previous-txg wait, and so one full
#	transaction group sync, for every block cloned.
#

verify_runnable "global"

typeset -r CD_N=64
typeset -r CD_SAVED=$(get_tunable CLONEDUP_APPLY_BLOCKS_PER_TXG)

function cleanup
{
	log_must set_tunable32 CLONEDUP_APPLY_BLOCKS_PER_TXG $CD_SAVED
	log_must restore_tunable TXG_TIMEOUT
	clonedup_cleanup
}

log_assert "clonedup batches its clones into shared transactions"
log_onexit cleanup
log_must save_tunable TXG_TIMEOUT

function fill_pool
{
	clonedup_pool_create
	clonedup_write /$TESTPOOL/a $CD_N
	clonedup_dup /$TESTPOOL/a /$TESTPOOL/b
	clonedup_sync
}

fill_pool
clonedup_run
clonedup_check_shared $TESTPOOL a $TESTPOOL b \
    "$(clonedup_all_blocks $CD_N)"
clonedup_kstat_is clones $CD_N
log_note "batches: $(clonedup_kstat batches) for $CD_N clones"
log_must [ "$(clonedup_kstat batches)" -ge 1 ]
log_must [ "$(clonedup_kstat batches)" -le 8 ]
clonedup_leakcheck

# a batch of one is one transaction per block
log_must zpool destroy -f $TESTPOOL
log_must set_tunable32 CLONEDUP_APPLY_BLOCKS_PER_TXG 1
log_must set_tunable32 TXG_TIMEOUT 1
fill_pool
clonedup_run
clonedup_check_shared $TESTPOOL a $TESTPOOL b \
    "$(clonedup_all_blocks $CD_N)"
clonedup_kstat_is clones $CD_N
clonedup_kstat_is batches $CD_N
clonedup_leakcheck

log_pass "clonedup batches its clones into shared transactions"
