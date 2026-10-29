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
#	The counting pre-pass changes what the index costs, not what
#	the run does.
#
# STRATEGY:
#	1. Run with the filter forced off and record what was cloned.
#	2. Rebuild the same pool and run with it forced on.
#	3. The same blocks are shared either way, and the filter
#	   reports having dropped blocks that could not pair.
#

verify_runnable "global"

function cleanup
{
	log_must restore_tunable CLONEDUP_INDEX_FILTER
	clonedup_cleanup
}

log_assert "the counting pre-pass does not change the result"
log_onexit cleanup
log_must save_tunable CLONEDUP_INDEX_FILTER

# One pair that can be cloned, and unique data that cannot.  The
# filter must keep the first and drop the second.
function build_pool
{
	clonedup_pool_create
	clonedup_write /$TESTPOOL/a
	clonedup_dup /$TESTPOOL/a /$TESTPOOL/b
	clonedup_write /$TESTPOOL/unique1
	clonedup_write /$TESTPOOL/unique2
	clonedup_sync
}

log_must set_tunable32 CLONEDUP_INDEX_FILTER 0
build_pool
clonedup_run
clonedup_check_shared $TESTPOOL a $TESTPOOL b "$(clonedup_all_blocks)"
typeset plain_applied=$(clonedup_jstat clonedup_applied)
typeset plain_saved=$(clonedup_jstat clonedup_saved)
typeset plain_held=$(clonedup_jstat clonedup_saved_snapheld)
clonedup_assert_counts "$plain_applied" "$plain_saved" "$plain_held"
clonedup_kstat_is count_walks 0
clonedup_leakcheck
log_must zpool destroy $TESTPOOL

log_must set_tunable32 CLONEDUP_INDEX_FILTER 1
build_pool
clonedup_run
clonedup_check_shared $TESTPOOL a $TESTPOOL b "$(clonedup_all_blocks)"
clonedup_jstat_is clonedup_applied $plain_applied
clonedup_jstat_is clonedup_saved $plain_saved
clonedup_jstat_is clonedup_saved_snapheld $plain_held
clonedup_jstat_is errors 0
log_must cmp /$TESTPOOL/a /$TESTPOOL/b

# The pre-pass ran, and it kept entries out.  unique1 and unique2
# share no key with anything, so every one of their blocks is
# declined; a false positive would only lower this, never zero it.
clonedup_kstat_gt count_walks 0
clonedup_kstat_gt filter_dropped 0
clonedup_leakcheck

log_pass "the counting pre-pass does not change the result"
