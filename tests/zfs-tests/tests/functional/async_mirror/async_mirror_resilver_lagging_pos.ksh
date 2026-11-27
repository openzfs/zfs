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
#	When the leader moved on alone while the lagging child was behind,
#	and the lagging child returns, the lagging child gets a full
#	resilver, not only the txgs above its own.  The import does not
#	trust what the lagging child holds.  After the resilver, the
#	async mirror starts again on the lagging child, the lagging child
#	alone holds the whole pool, and the next import keeps the async
#	mirror on with the same leader.
#
# STRATEGY:
#	1. Create a pool with a two-way mirror and turn the async mirror on.
#	2. Write files a, wait until the lagging child has caught up, and
#	   list the blocks of files a on the lagging child.
#	3. Stop the copy, write files b, and export the pool.
#	4. Hide the lagging child, import the leader alone, write files c,
#	   and export the pool.
#	5. Damage the listed blocks on the lagging child.  A resilver of
#	   only the txgs above the lagging child's txg would not rewrite
#	   them.
#	6. Bring the lagging child back, import the pool, and wait for the
#	   resilver, for the async mirror to start again, and for the copy.
#	7. Import the lagging child alone, read-only.  Verify files a, b and
#	   c, with no errors.
#	8. Import the pool.  Verify that the async mirror is on.
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
typeset blocks="$(for f in /$TESTPOOL/a.0 /$TESTPOOL/a.1; do
	list_file_blocks $f
done | awk -v p=$ASYNC_MIRROR_LAGGING '$1 == "L0" && $2 == p {
	print $3, $4 }')"
[[ -n "$blocks" ]] || log_fail "No block of files a on the lagging child"
async_mirror_export_behind b 2
async_mirror_suspend_copy 0

async_mirror_import_leader
async_mirror_write_files c 2
typeset digests="$(async_mirror_digests a 2)
$(async_mirror_digests b 2)
$(async_mirror_digests c 2)"
log_must zpool export $TESTPOOL

echo "$blocks" | while read offset length; do
	log_must dd if=/dev/urandom of=$ASYNC_MIRROR_LAGGING_HIDDEN bs=512 \
	    count=$length seek=$offset conv=notrunc
done
async_mirror_unhide_lagging

async_mirror_import
log_must wait_async_mirror_resilvered
log_must test "$(get_async_mirror_prop write_mode)" == sync
log_must zpool set write_mode=behind $TESTPOOL $ASYNC_MIRROR_LAGGING
log_must wait_async_mirror_state "behind|current"
log_must wait_async_mirror_idle
log_must zpool export $TESTPOOL

log_must mv $ASYNC_MIRROR_LEADER $ASYNC_MIRROR_HIDDEN
log_must zpool import -o readonly=on -d $ASYNC_MIRROR_DIR $TESTPOOL
log_must test "$digests" == "$(async_mirror_digests a 2)
$(async_mirror_digests b 2)
$(async_mirror_digests c 2)"
log_must check_pool_status $TESTPOOL errors "No known data errors"
log_must zpool export $TESTPOOL
async_mirror_unhide_leader

async_mirror_import
log_must test "$(get_async_mirror_stat active)" == 1
log_must verify_pool $TESTPOOL

log_pass "A lagging child that returns after the leader moved on is " \
    "resilvered in full."
