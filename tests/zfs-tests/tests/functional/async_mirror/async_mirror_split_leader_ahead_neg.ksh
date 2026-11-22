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
#	When the lagging child was imported alone and written, and the
#	leader then returns with a higher txg, a read-write import of the
#	pool is refused.  Without the check, the leader wins silently and
#	the writes made on the lagging child are lost.
#
# STRATEGY:
#	1. Create a pool with a two-way mirror and turn the async mirror on.
#	2. Write files a, and wait until the lagging child has caught up.
#	3. Stop the copy, write files b, and export the pool.
#	4. Hide the leader.  Import the lagging child alone, write file c,
#	   and export the pool.  Verify that the leader has the higher txg.
#	5. Bring the leader back.  Verify that the import fails with the
#	   message for copies that were written separately.
#

verify_runnable "global"

log_onexit cleanup_async_mirror

async_mirror_create_pool
async_mirror_enable

async_mirror_write_files a 2
log_must wait_async_mirror_idle
async_mirror_export_behind b 16

async_mirror_suspend_copy 0
async_mirror_import_lagging
async_mirror_write /$TESTPOOL/c 4
log_must zpool export $TESTPOOL

typeset -i leader=$(get_ring_max_txg $ASYNC_MIRROR_HIDDEN)
typeset -i lagging=$(get_ring_max_txg $ASYNC_MIRROR_LAGGING)
log_note "leader at txg $leader, lagging child at txg $lagging"
log_must test $leader -gt $lagging

async_mirror_unhide_leader
typeset out
out=$(zpool import -d $ASYNC_MIRROR_DIR $TESTPOOL 2>&1) &&
    log_fail "The import of copies written separately succeeded"
log_note "$out"
echo "$out" | grep -q "written separately" ||
    log_fail "The import failed with another error"
log_mustnot poolexists $TESTPOOL

log_pass "Copies written separately are refused, leader ahead."
