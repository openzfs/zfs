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
#	A copy that the pool configuration marks offline, and that was
#	written apart from the pool, is not read by the import.  Then
#	"zpool online" refuses it, leaves it offline and writes nothing to
#	it, and the operator can replace it.
#
# STRATEGY:
#	1. Create a pool with a two-way mirror and turn the async mirror on.
#	2. Write files a, stop the copy, write files b, and export the pool.
#	3. Hide the leader, import the lagging child alone, write files c,
#	   and export the pool.  Bring the leader back.
#	4. Hide the lagging child, import the leader alone, take the lagging
#	   child offline, write files d (the leader ends above the lagging
#	   child), and export the pool.  Bring the lagging child back.
#	5. Import both copies.  The import succeeds, because it does not read
#	   the offline child.
#	6. Verify that "zpool online" of the lagging child fails with the
#	   "written separately" error, that the child stays offline, that no
#	   resilver starts, and that no uberblock of the child changes.
#	7. Verify the data of the leader.
#	8. Export the pool, clear the label of the lagging child, import the
#	   pool, replace the child, and wait for the resilver.  Verify that
#	   files c are gone and that a scrub finds no errors.
#

verify_runnable "global"

log_onexit cleanup_async_mirror

async_mirror_create_pool
typeset guid=$(get_leaf_guid $ASYNC_MIRROR_LAGGING)
async_mirror_enable

async_mirror_write_files a 2
log_must wait_async_mirror_idle
async_mirror_suspend_copy 1
#
# Many txgs, not many bytes: the leader must end above the lagging child
# after the import of the child alone.  24 files of 128 KiB fit in the
# 128 MiB mirror.
#
async_mirror_write_files b 24 1
log_must test "$(get_async_mirror_stat pending_txgs)" -gt 0
log_must zpool export $TESTPOOL
async_mirror_suspend_copy 0

async_mirror_import_lagging
async_mirror_write_files c 2
log_must zpool export $TESTPOOL
async_mirror_unhide_leader

async_mirror_import_leader
log_must zpool offline $TESTPOOL $guid
async_mirror_write_files d 4 1
typeset digests="$(async_mirror_digests a 2)
$(async_mirror_digests b 24)
$(async_mirror_digests d 4)"
log_must zpool export $TESTPOOL
async_mirror_unhide_lagging

typeset -i lagging=$(get_ring_max_txg $ASYNC_MIRROR_LAGGING)
typeset -i leader=$(get_ring_max_txg $ASYNC_MIRROR_LEADER)
log_must test $leader -gt $lagging

async_mirror_import
log_must check_state $TESTPOOL $ASYNC_MIRROR_LAGGING "OFFLINE"

typeset out
out=$(zpool online $TESTPOOL $ASYNC_MIRROR_LAGGING 2>&1) &&
    log_fail "zpool online of a copy written separately succeeded"
log_note "$out"
echo "$out" | grep -q "written separately" ||
    log_fail "zpool online failed with another error"
log_must check_state $TESTPOOL $ASYNC_MIRROR_LAGGING "OFFLINE"
log_must zpool sync -l $TESTPOOL
zpool status $TESTPOOL | grep -q "resilver in progress" &&
    log_fail "A resilver of the copy written separately started"
log_must test "$(get_ring_max_txg $ASYNC_MIRROR_LAGGING)" -eq $lagging

log_must test "$digests" == "$(async_mirror_digests a 2)
$(async_mirror_digests b 24)
$(async_mirror_digests d 4)"
log_mustnot test -e /$TESTPOOL/c.0

#
# The advice of the error: replace the copy.  Clear its label while the
# pool is exported, as the operator does for a copy that is to be replaced.
#
log_must zpool export $TESTPOOL
log_must zpool labelclear -f $ASYNC_MIRROR_LAGGING
async_mirror_import
log_must zpool replace -f $TESTPOOL $guid $ASYNC_MIRROR_LAGGING
log_must wait_async_mirror_resilvered

log_must test "$digests" == "$(async_mirror_digests a 2)
$(async_mirror_digests b 24)
$(async_mirror_digests d 4)"
log_mustnot test -e /$TESTPOOL/c.0
log_must verify_pool $TESTPOOL
log_must check_pool_status $TESTPOOL errors "No known data errors"

log_pass "zpool online refuses a copy that was written separately."
