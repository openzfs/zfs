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
#	A destination block that changed between the index walk and
#	the apply is skipped as stale, not cloned over.  The apply
#	re-reads every destination inside its transaction and
#	compares the block pointer against the one the walk recorded,
#	so a block rewritten and synced in between no longer matches
#	and must be left alone.
#

verify_runnable "global"

function cleanup
{
	log_must restore_tunable CLONEDUP_APPLY_ENABLED
	clonedup_cleanup
}

log_assert "a destination changed after indexing is skipped as stale"
log_onexit cleanup
log_must save_tunable CLONEDUP_APPLY_ENABLED

clonedup_pool_create

clonedup_write /$TESTPOOL/a
clonedup_dup /$TESTPOOL/a /$TESTPOOL/b
clonedup_sync

# Park the run in its apply phase so the index is built and nothing
# has been cloned yet.
log_must set_tunable32 CLONEDUP_APPLY_ENABLED 0
log_must zpool clonedup $TESTPOOL
clonedup_wait_applying

# Rewrite both copies with different content and sync, so every block
# pointer the walk recorded is now stale rather than merely dirty.
clonedup_write /$TESTPOOL/a
clonedup_write /$TESTPOOL/b
clonedup_sync

log_must set_tunable32 CLONEDUP_APPLY_ENABLED 1
log_must zpool wait -t clonedup $TESTPOOL

clonedup_jstat_gt clonedup_skipped_stale 0
clonedup_jstat_is clonedup_applied 0
clonedup_jstat_is errors 0
clonedup_check_shared $TESTPOOL a $TESTPOOL b ""

clonedup_leakcheck

log_pass "a destination changed after indexing is skipped as stale"
