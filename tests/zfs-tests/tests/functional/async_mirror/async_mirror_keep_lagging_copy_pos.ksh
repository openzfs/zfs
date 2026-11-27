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
#	When the lagging child was imported alone and written while it was
#	behind, a read-write import of both copies is refused, and the
#	message names the choice to keep one copy.  With that choice, the
#	import keeps the lagging copy and erases the leader's uberblocks. The
#	leader then gets a full resilver. The txgs that only the leader had are
#	lost, by the operator's choice.
#
# STRATEGY:
#	1. Create a pool with a two-way mirror and turn the async mirror on.
#	2. Write files a, and wait until the lagging child has caught up.
#	3. Stop the copy, write many files b, and export the pool.
#	4. Hide the leader and import the lagging child alone. Write files c
#	   and export the pool. Then import the leader alone and export it.
#	5. Bring the leader back.  Verify its txg is higher, then verify
#	   that an import is refused and names --async-mirror-keep.
#	6. Import with --async-mirror-keep and the lagging child guid. Verify
#	   files a and c, no files b, and wait for the full resilver of the
#	   leader.
#	7. Export and reimport. Verify kept data, and that the async mirror
#	   starts again on the kept copy.
#	8. Check that the resilvered leader can import alone.
#

verify_runnable "global"

function cleanup
{
	[[ -e $ASYNC_MIRROR_HIDDEN ]] && async_mirror_unhide_leader
	[[ -e $ASYNC_MIRROR_LAGGING_HIDDEN ]] && async_mirror_unhide_lagging
	cleanup_async_mirror
}

log_onexit cleanup

async_mirror_create_pool
async_mirror_enable

async_mirror_write_files a 2
log_must wait_async_mirror_idle
async_mirror_suspend_copy 1
#
# Many txgs, not many bytes: the leader must end above the lagging child
# after the import of the child alone.  24 files of 2 MiB do not fit the
# 128 MiB mirror: its slop space is half of the pool.
#
async_mirror_write_files b 24 1
log_must test "$(get_async_mirror_stat pending_txgs)" -gt 0
log_must zpool export $TESTPOOL
async_mirror_suspend_copy 0
typeset -i fork=$(get_ring_max_txg $ASYNC_MIRROR_LAGGING)
typeset -i leader=$(get_ring_max_txg $ASYNC_MIRROR_LEADER)
log_must test $leader -gt $fork

log_must mv $ASYNC_MIRROR_LEADER $ASYNC_MIRROR_HIDDEN
log_must zpool import -d $ASYNC_MIRROR_DIR $TESTPOOL
async_mirror_write_files c 2
typeset digests="$(async_mirror_digests a 2)
$(async_mirror_digests c 2)"
log_must zpool export $TESTPOOL
async_mirror_unhide_leader

# Leave the leader unmarked while it is the only copy.
log_must mv $ASYNC_MIRROR_LAGGING $ASYNC_MIRROR_LAGGING_HIDDEN
log_must zpool import -d $ASYNC_MIRROR_DIR $TESTPOOL
log_must test -e /$TESTPOOL/a.0
log_must test -e /$TESTPOOL/b.0
log_mustnot test -e /$TESTPOOL/c.0
log_must test "$(get_async_mirror_stat active)" -eq 0
log_must zpool export $TESTPOOL
async_mirror_unhide_lagging

#
# Keep the leader's configuration selected for the first two-copy import.
# The mirror is unmarked, so the import must preserve both histories until
# the operator chooses which copy to keep.
#
typeset -i kept=$(get_ring_max_txg $ASYNC_MIRROR_LAGGING)
typeset -i selected=$(get_ring_max_txg $ASYNC_MIRROR_LEADER)
log_must test $selected -gt $kept

typeset out
out=$(zpool import -f -d $ASYNC_MIRROR_DIR $TESTPOOL 2>&1) &&
    log_fail "The import of copies written separately succeeded"
log_note "$out"
echo "$out" | grep -q "written separately" ||
    log_fail "The import failed with another error"
echo "$out" | grep -q -- "--async-mirror-keep" ||
    log_fail "The message does not name the choice"
log_mustnot poolexists $TESTPOOL

# The failed import must not copy the selected uberblock to either leaf.
log_must test "$(get_ring_max_txg $ASYNC_MIRROR_LAGGING)" -eq $kept
log_must test "$(get_ring_max_txg $ASYNC_MIRROR_LEADER)" -eq $selected

# Each copy must still load its own history without a rewind.
log_must mv $ASYNC_MIRROR_LEADER $ASYNC_MIRROR_HIDDEN
log_must zdb -e -p $ASYNC_MIRROR_DIR $TESTPOOL
log_must test "$(get_ring_max_txg $ASYNC_MIRROR_LAGGING)" -eq $kept
async_mirror_unhide_leader

log_must mv $ASYNC_MIRROR_LAGGING $ASYNC_MIRROR_LAGGING_HIDDEN
log_must zdb -e -p $ASYNC_MIRROR_DIR $TESTPOOL
log_must test "$(get_ring_max_txg $ASYNC_MIRROR_LEADER)" -eq $selected
async_mirror_unhide_lagging

typeset guid=$(get_leaf_guid $ASYNC_MIRROR_LAGGING)
log_must zpool import -f -d $ASYNC_MIRROR_DIR --async-mirror-keep=$guid \
    $TESTPOOL
log_must test "$(get_async_mirror_stat active)" == 0
log_must test "$digests" == "$(async_mirror_digests a 2)
$(async_mirror_digests c 2)"
log_mustnot test -e /$TESTPOOL/b.0
log_must wait_async_mirror_resilvered
log_must check_state $TESTPOOL $ASYNC_MIRROR_LEADER "ONLINE"
log_must zpool export $TESTPOOL

async_mirror_import
log_must test "$digests" == "$(async_mirror_digests a 2)
$(async_mirror_digests c 2)"
# The import leaves the kept copy in sync mode after the leader's full
# resilver. The administrator starts the async mirror again.
log_must test "$(get_async_mirror_prop write_mode)" == sync
log_must zpool set write_mode=behind $TESTPOOL $ASYNC_MIRROR_LAGGING
log_must wait_async_mirror_state "behind|current"
log_must wait_async_mirror_idle
log_must verify_pool $TESTPOOL
log_must zpool export $TESTPOOL
log_must mv $ASYNC_MIRROR_LAGGING $ASYNC_MIRROR_LAGGING_HIDDEN
log_must zdb -e -p $ASYNC_MIRROR_DIR $TESTPOOL
async_mirror_unhide_lagging
async_mirror_import
log_must test "$digests" == "$(async_mirror_digests a 2)
$(async_mirror_digests c 2)"
log_must verify_pool $TESTPOOL

log_pass "The operator can keep one copy and later resilver the other."
