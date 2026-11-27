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
#	Replace an unavailable failed child without waiting for its labels.
#	A failed child whose DTL is not yet durable can be replaced and
#	resilvered without waiting for its labels.
#
# STRATEGY:
#	1. Create a pool, set a DTL delay, enable the async mirror, write two
#	   before files, record their digests, and wait until idle.
#	2. Inject write errors on the lagging child, write a failed file, and
#	   wait until the copy fails.  Clear the errors, sync, and wait until
#	   the DTL txg is set while the child DTL txg stays zero.
#	3. Hide the lagging device path and reopen the pool.  Create a
#	   replacement device and replace the missing child, then sync.
#	4. Wait until the replacement is resilvered, reaches behind or current
#	   state, and the mirror is idle.  Verify the before digests.
#	5. Verify that the replacement is the lagging child.  Verify the pool.
#	6. Export, import, recheck the lagging property and digests, and
#	   verify the pool.
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
typeset digests="$(async_mirror_digests before 2)"
log_must wait_async_mirror_idle

typeset guid=$(get_leaf_guid $ASYNC_MIRROR_LAGGING)
log_must zinject -d $guid -e io -T write $TESTPOOL
async_mirror_write_files failed 1
log_must wait_async_mirror_failed
log_must zinject -c all
sync_pool $TESTPOOL
log_must wait_async_mirror_stat dtl_txg -gt 0
log_must test "$(get_async_mirror_stat dtl_child_txg)" -eq 0

# Reopen the pool after removing the device path to model a missing leaf.
log_must mv $ASYNC_MIRROR_LAGGING $ASYNC_MIRROR_LAGGING_HIDDEN
log_must zpool reopen $TESTPOOL
log_must truncate -s $ASYNC_MIRROR_VDEV_SIZE $ASYNC_MIRROR_REPLACEMENT
log_must timeout 30 zpool replace -f $TESTPOOL \
    $ASYNC_MIRROR_LAGGING $ASYNC_MIRROR_REPLACEMENT
log_must zpool sync $TESTPOOL
log_must wait_async_mirror_resilvered
log_must test "$(get_async_mirror_prop write_mode \
    $ASYNC_MIRROR_REPLACEMENT)" == sync
log_must zpool set write_mode=behind $TESTPOOL $ASYNC_MIRROR_REPLACEMENT
log_must wait_async_mirror_state "behind|current" \
    $ASYNC_MIRROR_TIMEOUT $ASYNC_MIRROR_REPLACEMENT
log_must wait_async_mirror_idle
log_must test "$digests" == "$(async_mirror_digests before 2)"

log_must test "$(get_async_mirror_prop write_mode \
    $ASYNC_MIRROR_REPLACEMENT)" == behind
log_must verify_pool $TESTPOOL
log_must zpool export $TESTPOOL
async_mirror_import
log_must test "$(get_async_mirror_prop write_mode \
    $ASYNC_MIRROR_REPLACEMENT)" == behind
log_must test "$digests" == "$(async_mirror_digests before 2)"
log_must verify_pool $TESTPOOL

log_pass "A missing failed child can be replaced and resilvered."
