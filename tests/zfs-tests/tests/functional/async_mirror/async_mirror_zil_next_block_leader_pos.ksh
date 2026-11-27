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
#	After a crash with a live intent log chain that ends in a log block
#	allocated ahead of its write, the leader alone replays the whole
#	chain, and a scrub finds no error.  When the lagging child returns
#	and is resilvered, it holds the replayed files too.  The leader must
#	keep the chain intact: the allocated and unwritten next block must
#	not stop the claim, and the copy must not take it.
#
# STRATEGY:
#	1. Create a pool with a two-way mirror and turn the async mirror on.
#	   Write files a, create the intent log with one synchronous write,
#	   and wait until the lagging child has caught up.
#	2. Stop the copy and freeze the pool.  Write files b with many
#	   synchronous writes, as async_mirror_zil_next_block_lagging_pos
#	   does, and keep their digests.  Export, as after a crash.
#	3. Import the leader alone.  Verify files a and b, and that a scrub
#	   finds no error on the leader.  Export.
#	4. Bring the lagging child back and import the pool.  Wait for the
#	   resilver.  Verify files a and b and the pool.
#	5. Export and import the lagging child alone.  Verify files a and b
#	   again and that a scrub finds no error.
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
typeset adigests="$(async_mirror_digests a 2)"
log_must dd if=/dev/zero of=/$TESTPOOL/sync conv=fdatasync,fsync bs=1 count=1
log_must wait_async_mirror_idle

async_mirror_suspend_copy 1
log_must zpool freeze $TESTPOOL
typeset -i i
for (( i = 0; i < 6; i++ )); do
	log_must dd if=/dev/urandom of=/$TESTPOOL/b.$i oflag=sync bs=1k \
	    count=8
	(( i == 2 )) && log_must zpool sync -l $TESTPOOL
done
log_must dd if=/dev/urandom of=/$TESTPOOL/b.big oflag=sync bs=128k count=4
typeset bdigests=""
for (( i = 0; i < 6; i++ )); do
	bdigests="$bdigests $(xxh128digest /$TESTPOOL/b.$i)"
done
bdigests="$bdigests $(xxh128digest /$TESTPOOL/b.big)"
log_must zpool export $TESTPOOL
async_mirror_suspend_copy 0

function async_mirror_b_digests
{
	typeset out=""

	for (( i = 0; i < 6; i++ )); do
		out="$out $(xxh128digest /$TESTPOOL/b.$i)"
	done
	echo "$out $(xxh128digest /$TESTPOOL/b.big)"
}

async_mirror_import_leader
log_must test "$adigests" == "$(async_mirror_digests a 2)"
log_must test "$bdigests" == "$(async_mirror_b_digests)"
async_mirror_check_clean $ASYNC_MIRROR_LEADER
log_must zpool export $TESTPOOL
async_mirror_unhide_lagging

async_mirror_import
log_must wait_async_mirror_resilvered
log_must test "$adigests" == "$(async_mirror_digests a 2)"
log_must test "$bdigests" == "$(async_mirror_b_digests)"
async_mirror_clear_import_errors $ASYNC_MIRROR_LAGGING
log_must verify_pool $TESTPOOL
log_must zpool export $TESTPOOL

async_mirror_import_lagging
log_must test "$adigests" == "$(async_mirror_digests a 2)"
log_must test "$bdigests" == "$(async_mirror_b_digests)"
async_mirror_check_clean $ASYNC_MIRROR_LAGGING
log_must zpool export $TESTPOOL
async_mirror_unhide_leader

log_pass "The leader replays a log with an unwritten next block."
