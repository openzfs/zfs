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
#	The copy to the lagging child copies a range that a transaction
#	group allocated and that no block pointer of that transaction group
#	leads to.  It reads the range from the leader as it is, and the
#	copy_rest_bytes and copy_rest_ranges counters count it.  Such a range
#	is expected only with deduplication, where a dedup entry alone can
#	point to a copy of a block, and that case cannot be made on demand.
#	So the test makes the copy ignore one block pointer of each
#	transaction group with a tunable: the ranges of that block are then
#	ranges that no block pointer leads to.
#
# STRATEGY:
#	1. Create a pool with a two-way mirror and turn the async mirror on.
#	   Write files a, and wait until the lagging child has caught up.
#	   Verify that the counters are 0.
#	2. For several numbers, make the copy ignore the block pointer with
#	   that number in each transaction group, write a file, and wait
#	   until the lagging child has caught up.  A small number selects a
#	   block of pool metadata, and a larger one a block further on in
#	   the transaction group, if it has that many.
#	3. Verify that the counters count ranges and bytes, and that the copy
#	   neither failed nor froze.
#	4. Export the pool and import the lagging child alone.  Verify the
#	   files, and that a scrub finds no error: each ignored block is on
#	   the lagging child, with the bytes of the leader.
#

verify_runnable "global"

function cleanup
{
	[[ -e $ASYNC_MIRROR_HIDDEN ]] && async_mirror_unhide_leader
	cleanup_async_mirror
}

log_onexit cleanup

log_assert "The copy reaches a range that no block pointer leads to."

async_mirror_create_pool
async_mirror_enable
async_mirror_write_files a 2
log_must wait_async_mirror_idle
log_must test "$(get_async_mirror_stat copy_rest_ranges)" == 0
log_must test "$(get_async_mirror_stat copy_rest_bytes)" == 0

typeset -i n
typeset -i files=0
for n in 1 3 7 15 31 63; do
	log_must async_mirror_set_tunable32 ASYNC_MIRROR_INJECT_REST $n
	async_mirror_write /$TESTPOOL/b.$files 16
	sync_pool $TESTPOOL
	log_must wait_async_mirror_idle
	(( files++ ))
done
log_must async_mirror_set_tunable32 ASYNC_MIRROR_INJECT_REST 0

typeset -i ranges=$(get_async_mirror_stat copy_rest_ranges)
typeset -i bytes=$(get_async_mirror_stat copy_rest_bytes)
log_note "$ranges ranges, $bytes bytes with no block pointer"
log_must test $ranges -ge 1
log_must test $bytes -ge $(( ranges * 512 ))
log_must test "$(get_async_mirror_stat failed)" == 0
log_must test "$(get_async_mirror_stat frozen)" == 0
typeset digests="$(async_mirror_digests a 2; async_mirror_digests b $files)"

log_must zpool export $TESTPOOL
async_mirror_import_lagging
log_must test "$digests" == \
    "$(async_mirror_digests a 2; async_mirror_digests b $files)"
log_must zpool scrub -w $TESTPOOL
log_must check_pool_status $TESTPOOL errors "No known data errors"
log_must test "$(async_mirror_cksum_errors $ASYNC_MIRROR_LAGGING)" == 0
log_must zpool export $TESTPOOL
async_mirror_unhide_leader

log_pass "The copy reaches a range that no block pointer leads to."
