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
#	The counting pre-pass sizes its filter from the last run's
#	count only when that run finished over the same window.  A
#	quick run counts the blocks born since the last full or
#	default run, and a full run's count of the whole pool would
#	size the quick run's filter many times too large.  Without a
#	count for its window, the quick run takes the smallest filter.
#	A run that replaces one in progress takes no filter, since
#	the run it replaces holds its index and filter until then.
#
# STRATEGY:
#	1. Force the pre-pass on and write 8192 blocks of 4K.
#	2. Run a full clonedup twice.  The second run sizes its filter
#	   from the first run's count, past the smallest size.
#	3. Write a pair of new files and run a quick clonedup.
#	4. Check that the quick run's filter has the smallest slot
#	   count, and that the run still cloned the pair.
#	5. Hold another quick run in its counting pass and replace it
#	   with a full run.  Check that the full run has no filter
#	   and still finishes.
#

verify_runnable "global"

typeset -ri min_slots=65536

function cleanup
{
	log_must restore_tunable CLONEDUP_INDEX_FILTER
	clonedup_cleanup
}

log_assert "a quick run sizes its filter for its own window"
log_onexit cleanup
log_must save_tunable CLONEDUP_INDEX_FILTER
log_must set_tunable32 CLONEDUP_INDEX_FILTER 1

clonedup_pool_create -O recordsize=4k
log_must dd if=/dev/urandom of=/$TESTPOOL/a bs=4k count=8192 \
    status=none
clonedup_sync

clonedup_run -f
clonedup_run -f
typeset -i full=$(clonedup_record filter_slots)
log_note "full run: $full filter slots"
log_must [ $full -gt $min_slots ]

# 128 blocks of 4K each
clonedup_write /$TESTPOOL/b 4
clonedup_dup /$TESTPOOL/b /$TESTPOOL/c
clonedup_sync
clonedup_run -q
typeset -i quick=$(clonedup_record filter_slots)
log_note "quick run: $quick filter slots"
log_must [ $quick -eq $min_slots ]
clonedup_jstat_is errors 0
clonedup_check_shared $TESTPOOL b $TESTPOOL c \
    "$(clonedup_all_blocks 128)"

clonedup_hold
log_must zpool clonedup -q $TESTPOOL
clonedup_wait_running
clonedup_record_is filter_slots $min_slots
log_must zpool clonedup -f $TESTPOOL
clonedup_record_is filter_slots 0
clonedup_release
log_must zpool wait -t clonedup $TESTPOOL
clonedup_jstat_is state FINISHED
clonedup_jstat_is errors 0
clonedup_leakcheck

log_pass "a quick run sizes its filter for its own window"
