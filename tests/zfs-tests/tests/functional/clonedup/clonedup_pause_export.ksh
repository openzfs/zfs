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
#	A paused run stays paused across an export and an import, as
#	a paused scrub does.  The import restarts its partition but
#	keeps its options, and the same request resumes it.
#
# STRATEGY:
#	1. Start a full run, hold it in the index walk and pause it.
#	2. Export and import the pool, release the hold, and let the
#	   restart and a few more txgs go by.
#	3. Check that the run is paused and still a full run.
#	4. Resume it with -f and check that it finishes and shares
#	   the blocks.
#

verify_runnable "global"

function cleanup
{
	clonedup_release
	clonedup_cleanup
}

log_assert "a paused run stays paused across export and import"
log_onexit cleanup

clonedup_pool_create
clonedup_write /$TESTPOOL/a
clonedup_dup /$TESTPOOL/a /$TESTPOOL/b
clonedup_sync

clonedup_hold
log_must zpool clonedup -f $TESTPOOL
clonedup_wait_running
log_must zpool clonedup -p $TESTPOOL
log_must clonedup_is_paused

log_must zpool export $TESTPOOL
log_must zpool import $TESTPOOL
clonedup_release
typeset -i i
for ((i = 0; i < 5; i++)); do
	log_must zpool sync $TESTPOOL
	sleep 1
done
log_must clonedup_is_paused
log_must eval "zpool status -j $TESTPOOL | grep -q '\"full\":true'"

log_must zpool clonedup -f $TESTPOOL
log_mustnot clonedup_is_paused
log_must zpool wait -t clonedup $TESTPOOL
clonedup_jstat_is state FINISHED
clonedup_check_shared $TESTPOOL a $TESTPOOL b "$(clonedup_all_blocks)"
clonedup_leakcheck

log_pass "a paused run stays paused across export and import"
