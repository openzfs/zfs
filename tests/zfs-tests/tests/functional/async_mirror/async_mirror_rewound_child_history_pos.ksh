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
#	When the leader alone is rewound to a txg below the newest txg of
#	the lagging child and imported read-write, the lagging child holds
#	the history that the rewind discarded.  The rewind is the operator's
#	choice to discard that history, so when the lagging child returns,
#	the import is accepted, and the lagging child gets a full resilver,
#	not a resilver from the fork only: its blocks from below the fork are
#	repaired too.  Its data then equals the rewound state.
#
# STRATEGY:
#	1. Create a pool with a two-way mirror and turn the async mirror on.
#	2. Write files a, wait until the lagging child has caught up, and
#	   keep the txg of the pool.  Note the blocks of files a on the
#	   lagging child.
#	3. Write files b, wait until the lagging child has caught up, and
#	   export the pool.
#	4. Hide the lagging child.  Import the leader alone with -T at the
#	   kept txg, and export the pool.
#	5. Overwrite the blocks of files a on the lagging child, which are
#	   below the fork, and bring the lagging child back.
#	6. Import the pool.  Verify that the import is accepted.  Wait for
#	   the resilver.  Read no file, so that no repair of a read can
#	   hide a resilver that missed the blocks below the fork.
#	7. Import the lagging child alone, read-only.  Verify files a, that
#	   files b are gone, and that there are no errors.
#	8. Import the pool.  Verify files a, that files b are gone, and the
#	   pool.
#

verify_runnable "global"

function cleanup
{
	[[ -e $ASYNC_MIRROR_HIDDEN ]] && async_mirror_unhide_leader
	[[ -e $ASYNC_MIRROR_LAGGING_HIDDEN ]] && async_mirror_unhide_lagging
	cleanup_async_mirror
}

log_onexit cleanup

#
# The rewind goes back about 30 txgs: the copy of files b to the lagging
# child syncs many txgs.  A rewind fails when a block that the kept txg
# needs was freed and reused since, and the import then goes back to an
# older txg, before the async mirror was on.
#
ASYNC_MIRROR_VDEV_SIZE=1g

async_mirror_create_pool
async_mirror_enable

async_mirror_write_files a 2
async_mirror_suspend_release 1
log_must wait_async_mirror_idle
typeset digests="$(async_mirror_digests a 2)"
typeset -i txg=$(get_async_mirror_pool_txg)
typeset blocks="$(for f in /$TESTPOOL/a.0 /$TESTPOOL/a.1; do
	list_file_blocks $f
done | awk -v p=$ASYNC_MIRROR_LAGGING '$1 == "L0" && $2 == p {
	print $3, $4 }')"
[[ -n "$blocks" ]] || log_fail "No block of files a on the lagging child"
async_mirror_write_files b 2
log_must wait_async_mirror_idle
log_must zpool export $TESTPOOL

typeset -i newest=$(get_ring_max_txg $ASYNC_MIRROR_LAGGING)
log_note "rewind txg $txg, newest txg of the lagging child $newest"
log_must test $newest -gt $txg

log_must mv $ASYNC_MIRROR_LAGGING $ASYNC_MIRROR_LAGGING_HIDDEN
async_mirror_import_txg $txg
async_mirror_suspend_release 0
log_must test "$(get_async_mirror_lineage lineage_parent)" != 0
log_must zpool export $TESTPOOL

echo "$blocks" | while read offset length; do
	log_must dd if=/dev/urandom of=$ASYNC_MIRROR_LAGGING_HIDDEN bs=512 \
	    count=$length seek=$offset conv=notrunc
done
async_mirror_unhide_lagging

async_mirror_import
log_must wait_async_mirror_resilvered
async_mirror_clear_import_errors $ASYNC_MIRROR_LAGGING
log_must zpool export $TESTPOOL

log_must mv $ASYNC_MIRROR_LEADER $ASYNC_MIRROR_HIDDEN
log_must zpool import -o readonly=on -d $ASYNC_MIRROR_DIR $TESTPOOL
log_must test "$digests" == "$(async_mirror_digests a 2)"
log_mustnot test -e /$TESTPOOL/b.0
log_must check_pool_status $TESTPOOL errors "No known data errors"
log_must zpool export $TESTPOOL
async_mirror_unhide_leader

async_mirror_import
log_must test "$digests" == "$(async_mirror_digests a 2)"
log_mustnot test -e /$TESTPOOL/b.0
log_must verify_pool $TESTPOOL

log_pass "A lagging child that holds history a rewind discarded is " \
    "accepted and resilvered in full."
