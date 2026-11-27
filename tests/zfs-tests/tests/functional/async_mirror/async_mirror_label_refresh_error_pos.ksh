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
#	A failed label refresh does not fail pool import. The child stays online
#	and resilvers after the write errors are clear.
#
# STRATEGY:
#	1. Turn the async mirror on and wait until its child has caught up.
#	2. Unmount the pool, inject flush errors, and unload it.
#	3. Open the pool. Verify label refresh failed, import succeeded, and
#	   async mirroring ended with the child online.
#	4. Clear the errors and verify that the child resilvers.
#

verify_runnable "global"

function cleanup
{
	zinject -c all >/dev/null 2>&1
	cleanup_async_mirror
}

function label_refresh_failure_count
{
	kstat dbgmsg | awk -v pool="spa_load($TESTPOOL," \
	    -v msg="async mirror: label refresh failed for child" '
	    index($0, pool) && index($0, msg) { n++ }
	    END { print n + 0 }'
}

log_onexit cleanup

async_mirror_create_pool
async_mirror_enable
async_mirror_write_files a 2
log_must wait_async_mirror_idle

typeset guid=$(get_leaf_guid $ASYNC_MIRROR_LAGGING)
typeset -i failures_before=$(label_refresh_failure_count)
log_must zfs unmount $TESTPOOL
log_must zinject -u -d $guid -e io -T flush $TESTPOOL
log_must zpool status $TESTPOOL
typeset -i failures_after=$(label_refresh_failure_count)
log_must test $failures_after -gt $failures_before
log_must test "$(get_async_mirror_stat active)" -eq 0
log_must check_state $TESTPOOL $ASYNC_MIRROR_LAGGING "ONLINE"
log_must zinject -c all
log_must zpool clear $TESTPOOL
log_must wait_async_mirror_resilvered
log_must zfs mount $TESTPOOL

log_must test "$(get_async_mirror_stat active)" -eq 0
log_must test -e /$TESTPOOL/a.1
log_must verify_pool $TESTPOOL

log_pass "A failed child label refresh does not block pool import."
