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
# Copyright (c) 2026, John White.
#

. $STF_SUITE/tests/functional/async_mirror/async_mirror.kshlib

#
# DESCRIPTION:
#	A lagging child that holds only the txg in which the property
#	turned the async mirror on has the lineage of the pool.  When that child is
#	imported alone and written, and the leader then returns, a
#	read-write import of the pool is refused.
#
# STRATEGY:
#	1. Create a pool with a two-way mirror, and write files a.
#	2. Stop the copy, turn the async mirror on, write files b, and export the
#	   pool.  The lagging child keeps the txg of the property change.
#	3. Verify that the labels of the lagging child hold a lineage.
#	4. Hide the leader.  Import the lagging child alone, write file c,
#	   and export the pool.
#	5. Bring the leader back.  Verify that the import fails with the
#	   message for copies that were written separately.
#

verify_runnable "global"

log_onexit cleanup_async_mirror

async_mirror_create_pool
async_mirror_write_files a 2

async_mirror_suspend_copy 1
async_mirror_enable
async_mirror_write_files b 4
log_must test "$(get_async_mirror_stat pending_txgs)" -gt 0
log_must zpool export $TESTPOOL

typeset lineage=$(get_label_lineage $ASYNC_MIRROR_LAGGING)
log_note "lineage in the labels of the lagging child: $lineage"
log_must test "$lineage" != 0

async_mirror_suspend_copy 0
async_mirror_import_lagging
async_mirror_write /$TESTPOOL/c 4
log_must zpool export $TESTPOOL

async_mirror_unhide_leader
typeset out
out=$(zpool import -d $ASYNC_MIRROR_DIR $TESTPOOL 2>&1) &&
    log_fail "The import of copies written separately succeeded"
log_note "$out"
echo "$out" | grep -q "written separately" ||
    log_fail "The import failed with another error"
log_mustnot poolexists $TESTPOOL

log_pass "A child at the txg of the switch on is in the lineage of the pool."
