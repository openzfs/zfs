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
#	A backlog of the copy above its limit makes new writes wait for
#	the copy, but the pool still syncs txgs: the sync thread does not
#	wait for the lagging child.
#
# STRATEGY:
#	1. Create a pool with a two-way mirror, and turn the async mirror on.
#	2. Stop the copy, and write files in three txgs.  Set the limit of
#	   pending txgs to 1.
#	3. Make each I/O of the lagging child slow, and restart the copy.
#	4. Start a write in the background.  Verify that a sync of the
#	   pool ends while the backlog is still above the limit, and while
#	   the write still waits.
#	5. Verify that the write ends when the copy catches up, and that
#	   the kstat counts the time that it waited.
#	6. Clear the fault, and verify the files and the pool.
#

verify_runnable "global"

function cleanup
{
	zinject -c all > /dev/null 2>&1
	[[ -n $wpid ]] && kill $wpid > /dev/null 2>&1
	cleanup_async_mirror
}

typeset wpid=""
log_onexit cleanup

async_mirror_create_pool
async_mirror_enable
async_mirror_write_files a 1
log_must wait_async_mirror_idle

async_mirror_suspend_copy 1
async_mirror_write_files b 3
typeset digests="$(async_mirror_digests b 3)"
log_must test "$(get_async_mirror_stat pending_txgs)" -ge 3
log_must async_mirror_set_tunable64 ASYNC_MIRROR_MAX_PENDING_TXGS 1

log_must zinject -d $(get_leaf_guid $ASYNC_MIRROR_LAGGING) -D 1000:1 $TESTPOOL
typeset -i waited=$(get_async_mirror_stat throttled_ms)
async_mirror_suspend_copy 0

dd if=/dev/urandom of=/$TESTPOOL/w bs=128k count=1 2>/dev/null &
wpid=$!
sleep 1
log_must zpool sync -l $TESTPOOL
typeset -i pending=$(get_async_mirror_stat pending_txgs)
log_must test $pending -gt 1
log_must kill -0 $wpid

log_must wait $wpid
wpid=""
log_must zinject -c all
log_must async_mirror_set_tunable64 ASYNC_MIRROR_MAX_PENDING_TXGS 256
log_must wait_async_mirror_idle
log_must test "$(get_async_mirror_stat failed)" -eq 0
log_must test "$digests" == "$(async_mirror_digests b 3)"
log_must verify_pool $TESTPOOL

log_pass "A backlog over the limit makes writes wait, not the sync thread."
