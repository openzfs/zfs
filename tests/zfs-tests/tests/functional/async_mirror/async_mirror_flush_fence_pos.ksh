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
#	A failed flush on the child alone keeps the DTL fence closed.  Until
#	the failed child flushes its own DTL-bearing metadata, the DTL fence
#	stays closed.
#
# STRATEGY:
#	1. Create a pool, set a DTL delay, enable the async mirror, write two
#	   before files and wait until idle.
#	2. Inject write errors on the lagging child, write a failed file, and
#	   wait until the copy fails.  Clear the errors, sync, and wait until
#	   the DTL txg is set.  Suspend the release.
#	3. Inject flush errors on the lagging child, disable the DTL delay,
#	   and sync eight times.  Verify that the child DTL txg stays zero.
#	4. Clear the errors and wait until the child DTL txg becomes positive.
#	5. Resume the release, wait until the child is resilvered, and verify
#	   the pool.
#

verify_runnable "global"

function cleanup
{
	zinject -c all > /dev/null 2>&1
	cleanup_async_mirror
}

log_onexit cleanup

async_mirror_create_pool
log_must async_mirror_set_tunable32 ASYNC_MIRROR_INJECT_DTL_DELAY 100
async_mirror_enable
async_mirror_write_files before 2
log_must wait_async_mirror_idle

typeset guid=$(get_leaf_guid $ASYNC_MIRROR_LAGGING)
log_must zinject -d $guid -e io -T write $TESTPOOL
async_mirror_write_files failed 1
log_must wait_async_mirror_failed
log_must zinject -c all
sync_pool $TESTPOOL
log_must wait_async_mirror_stat dtl_txg -gt 0
async_mirror_suspend_release 1

# The parent flush succeeds even when the failed child's own flush fails.
log_must zinject -d $guid -e io -T flush $TESTPOOL
log_must async_mirror_set_tunable32 ASYNC_MIRROR_INJECT_DTL_DELAY 0
typeset -i i
for (( i = 0; i < 8; i++ )); do
	sync_pool $TESTPOOL
done
log_must test "$(get_async_mirror_stat dtl_child_txg)" -eq 0

log_must zinject -c all
log_must wait_async_mirror_stat dtl_child_txg -gt 0
async_mirror_suspend_release 0
log_must wait_async_mirror_resilvered
log_must verify_pool $TESTPOOL

log_pass "The failed child must flush its own DTL-bearing metadata."
