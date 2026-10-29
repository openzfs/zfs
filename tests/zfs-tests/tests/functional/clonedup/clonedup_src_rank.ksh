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
#	Which copy becomes the source does not change the result, so
#	the choice does not have to consult the BRT to make it.
#
# STRATEGY:
#	1. Build a group where one copy is already block cloned and
#	   the rest are not, so the copies differ in BRT state and in
#	   nothing else.
#	2. Run, and every copy shares one block with identical
#	   content.
#	3. Build the same group with the already-cloned copy written
#	   last, reversing which copy the BRT holds, and require the
#	   same outcome.
#
#	Source ranking reads the DCE_F_MAYBE_SHARED flag the index
#	stored and does not look the block up in the BRT.  A wrong
#	guess costs one BRT entry and must never change the outcome.
#

verify_runnable "global"

function cleanup
{
	rm -f $TEST_BASE_DIR/clonedup_src
	clonedup_cleanup
}

log_assert "the result does not depend on which copy is the source"
log_onexit cleanup

clonedup_pool_create
typeset all="$(clonedup_all_blocks)"

#
# cloned_first: b is a reflink of a, so the BRT already holds that
# block when the run starts.  c and d are plain copies of the same
# bytes and are not in the BRT at all.
#
clonedup_write /$TESTPOOL/a
clonedup_sync
log_must clonefile -f /$TESTPOOL/a /$TESTPOOL/b
log_must cp /$TESTPOOL/a $TEST_BASE_DIR/clonedup_src
clonedup_dup $TEST_BASE_DIR/clonedup_src /$TESTPOOL/c
clonedup_dup $TEST_BASE_DIR/clonedup_src /$TESTPOOL/d
clonedup_sync
clonedup_check_shared $TESTPOOL a $TESTPOOL b "$all"

clonedup_run
clonedup_check_shared $TESTPOOL a $TESTPOOL c "$all"
clonedup_check_shared $TESTPOOL a $TESTPOOL d "$all"
clonedup_check_shared $TESTPOOL b $TESTPOOL c "$all"
clonedup_jstat_is errors 0
clonedup_jstat_is clonedup_skipped_differs 0
for f in b c d; do
	log_must cmp /$TESTPOOL/a /$TESTPOOL/$f
done
clonedup_leakcheck
log_must zpool destroy $TESTPOOL

#
# cloned_last: same group, but the plain copies are written first and
# the reflink is made afterwards, so a different member of the group
# is the one the BRT holds.  Nothing else differs.
#
clonedup_pool_create
clonedup_write /$TESTPOOL/a
log_must cp /$TESTPOOL/a $TEST_BASE_DIR/clonedup_src
clonedup_dup $TEST_BASE_DIR/clonedup_src /$TESTPOOL/c
clonedup_dup $TEST_BASE_DIR/clonedup_src /$TESTPOOL/d
clonedup_sync
log_must clonefile -f /$TESTPOOL/d /$TESTPOOL/b
clonedup_sync
clonedup_check_shared $TESTPOOL d $TESTPOOL b "$all"

clonedup_run
clonedup_check_shared $TESTPOOL a $TESTPOOL c "$all"
clonedup_check_shared $TESTPOOL a $TESTPOOL d "$all"
clonedup_check_shared $TESTPOOL b $TESTPOOL c "$all"
clonedup_jstat_is errors 0
clonedup_jstat_is clonedup_skipped_differs 0
for f in b c d; do
	log_must cmp /$TESTPOOL/a /$TESTPOOL/$f
done

# Both halves reach the same end state.  The number of candidates
# applied is not compared between them.  Which copy becomes the
# source decides how many of the others have to be rewritten, so
# the counts can differ.
clonedup_jstat_gt clonedup_applied 0
clonedup_leakcheck

log_pass "the result does not depend on which copy is the source"
