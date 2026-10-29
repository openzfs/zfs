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
#	A read that fails while the apply reads a block pointer counts
#	as an error.  The failure says nothing about whether the block
#	changed, so it does not count as a stale skip.
#
# STRATEGY:
#	1. Write a file and a copy of it.  Export and import the pool,
#	   so that no buffer of either file stays cached.
#	2. Park a run in its apply phase.  Its walk has read the level
#	   1 blocks of both files by then, through the ARC alone.
#	3. Wait until the ARC lets those level 1 blocks go, then fail
#	   every read of them and flush the ARC.
#	4. Let the apply go on.  Each pointer it reads lives in one of
#	   them.  Check that the handlers failed reads, that nothing
#	   was cloned, and that the reads count as errors and not as
#	   stale skips.
#

verify_runnable "global"

function cleanup
{
	zinject -c all >/dev/null 2>&1
	zpool clear $TESTPOOL >/dev/null 2>&1
	log_must restore_tunable CLONEDUP_APPLY_ENABLED
	clonedup_cleanup
}

# The reads the handlers failed, over all of them.
function cd_re_injected
{
	zinject | awk '$1 ~ /^[0-9]+$/ { n += $NF }
	    END { print n + 0 }'
}

log_assert "a block pointer read that fails counts as an error"
log_onexit cleanup
log_must save_tunable CLONEDUP_APPLY_ENABLED

clonedup_pool_create
clonedup_write /$TESTPOOL/a
clonedup_dup /$TESTPOOL/a /$TESTPOOL/b
clonedup_sync
log_must zpool export $TESTPOOL
log_must zpool import $TESTPOOL

log_must set_tunable32 CLONEDUP_APPLY_ENABLED 0
log_must zpool clonedup $TESTPOOL
clonedup_wait_applying

# The walk prefetched the level 1 blocks.  A flush leaves a prefetched
# or indirect buffer in the ARC until it is older than
# zfs_arc_min_prescient_prefetch_ms, 6 seconds by default.
log_must sleep 8
log_must zinject -t data -l 1 -e io -T read -f 100 /$TESTPOOL/a
log_must zinject -a -t data -l 1 -e io -T read -f 100 /$TESTPOOL/b
log_must set_tunable32 CLONEDUP_APPLY_ENABLED 1
log_must zpool wait -t clonedup $TESTPOOL
typeset injected=$(cd_re_injected)
clonedup_assert_counts "$injected"
log_must [ $injected -gt 0 ]

log_must zinject -c all
log_must zpool clear $TESTPOOL

clonedup_check_shared $TESTPOOL a $TESTPOOL b ""
clonedup_jstat_is clonedup_applied 0
clonedup_jstat_is clonedup_skipped_stale 0
clonedup_jstat_gt errors 0
clonedup_leakcheck

log_pass "a block pointer read that fails counts as an error"
