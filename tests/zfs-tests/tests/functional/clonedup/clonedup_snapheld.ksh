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
#	A block held by a snapshot is the surviving copy.  A live
#	file's block that a snapshot also holds is redirected onto
#	the survivor.
#

verify_runnable "global"

function cleanup
{
	clonedup_cleanup
}

log_assert "snapshot-held blocks are sources, heads are redirected"
log_onexit cleanup

clonedup_pool_create
clonedup_write /$TESTPOOL/a
clonedup_sync
log_must zfs snapshot $TESTPOOL@s1

# a is held by s1: g must be redirected onto a, never the reverse.
clonedup_dup /$TESTPOOL/a /$TESTPOOL/g
clonedup_sync
clonedup_run
clonedup_check_shared $TESTPOOL a $TESTPOOL g "$(clonedup_all_blocks)"
clonedup_check_shared $TESTPOOL@s1 a $TESTPOOL a \
    "$(clonedup_all_blocks)"

# h is held by s2 as well: the head copy is redirected, s2 keeps it.
clonedup_dup /$TESTPOOL/a /$TESTPOOL/h
clonedup_sync
log_must zfs snapshot $TESTPOOL@s2
# written since s2, so the run may still rewrite the pool's root
clonedup_write /$TESTPOOL/n 1
clonedup_sync
clonedup_run
clonedup_check_shared $TESTPOOL a $TESTPOOL h "$(clonedup_all_blocks)"
clonedup_check_shared $TESTPOOL@s2 h $TESTPOOL h ""
clonedup_jstat_is clonedup_saved_snapheld \
    $((CD_BLOCKS * $(clonedup_blk_asize $TESTPOOL a)))
# zpool status -j reports the same saved total as the plain output.
typeset line=$(zpool status $TESTPOOL | grep 'clonedup completed')
typeset nice=$(zpool status -j $TESTPOOL | jq -r --arg p $TESTPOOL \
    '.pools[$p].scan_stats | "\(.clonedup_saved) saved" +
    " (\(.clonedup_saved_snapheld) held by snapshots)"')
log_note "status: '$line', status -j: '$nice'"
[[ $line == *", $nice in "* ]] ||
    log_fail "zpool status -j does not match the scan line"
typeset alloc_before=$(zpool get -Hpo value allocated $TESTPOOL)
log_must zfs destroy $TESTPOOL@s2
clonedup_sync
typeset alloc_after=$(zpool get -Hpo value allocated $TESTPOOL)
clonedup_assert_counts "$alloc_before" "$alloc_after"
log_must [ $alloc_after -lt $alloc_before ]
clonedup_leakcheck

log_pass "snapshot-held blocks are sources, heads are redirected"
