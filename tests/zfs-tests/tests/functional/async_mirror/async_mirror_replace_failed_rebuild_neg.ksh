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
# A failed lagging child cannot be replaced with sequential reconstruction.
#
# STRATEGY:
# 1. Stop record release, then fail replay before a resilver starts.
# 2. Try replace -s and check that the async mirror stays unchanged.
# 3. Let the resilver start and finish, then verify the pool.
#

verify_runnable "global"

function cleanup_async_mirror_replace_failed_rebuild
{
	cleanup_async_mirror
	log_must rm -f $TEST_BASE_DIR/async_mirror_replace_failed_rebuild.err
}

log_onexit cleanup_async_mirror_replace_failed_rebuild

async_mirror_create_pool
async_mirror_enable
async_mirror_write_files before 2
log_must wait_async_mirror_idle
async_mirror_suspend_release 1

typeset guid=$(get_leaf_guid $ASYNC_MIRROR_LAGGING)
log_must zinject -d $guid -e io -T write $TESTPOOL
async_mirror_write_files failed 2
log_must wait_async_mirror_failed
log_mustnot is_pool_resilvering $TESTPOOL

log_must truncate -s $ASYNC_MIRROR_VDEV_SIZE $ASYNC_MIRROR_REPLACEMENT
typeset replace_error=$TEST_BASE_DIR/async_mirror_replace_failed_rebuild.err
if timeout 60 zpool replace -s -f $TESTPOOL $ASYNC_MIRROR_LAGGING \
    $ASYNC_MIRROR_REPLACEMENT >$replace_error 2>&1; then
	log_fail "replace -s succeeded for a failed lagging child"
fi
grep -Fq 'a failed or frozen lagging child cannot be replaced with -s' \
	$replace_error ||
	log_fail "replace -s returned the wrong error" "$(cat $replace_error)"
log_must test "$(get_async_mirror_prop write_mode)" == behind
log_must test "$(get_async_mirror_prop state)" == failed

log_must zinject -c all
async_mirror_suspend_release 0
log_must zpool sync -l $TESTPOOL
log_must wait_async_mirror_resilvered
log_must verify_pool $TESTPOOL

log_pass "A failed child rejects replace -s before the mode changes."
