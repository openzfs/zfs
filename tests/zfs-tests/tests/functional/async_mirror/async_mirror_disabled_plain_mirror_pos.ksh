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
#	After the async mirror is turned off and its records are gone, the pool
#	has no lineage, and a pool that was written with one child missing
#	imports again with both, as a plain mirror does.
#
# STRATEGY:
#	1. Create a pool with a two-way mirror and turn the async mirror on.
#	2. Write files a, and wait until the lagging child has caught up.
#	3. Turn the async mirror off, and wait until the feature is no longer
#	   active.  Export and import the pool, and verify that it has no
#	   lineage.
#	4. Export the pool.  Hide the second child, import the first child
#	   alone, write files b, and export the pool.
#	5. Bring the second child back and import the pool.
#	6. Wait for the resilver.  Verify files a and b, and that a scrub
#	   finds no errors.
#

verify_runnable "global"

log_onexit cleanup_async_mirror

async_mirror_create_pool
async_mirror_enable

async_mirror_write_files a 2
log_must wait_async_mirror_idle
typeset digests="$(async_mirror_digests a 2)"

async_mirror_disable
log_must wait_async_mirror_feature enabled
log_must zpool export $TESTPOOL
async_mirror_import
log_must test "$(get_async_mirror_lineage lineage)" == 0
log_must zpool export $TESTPOOL

async_mirror_import_leader
async_mirror_write_files b 2
digests="$digests
$(async_mirror_digests b 2)"
log_must zpool export $TESTPOOL
async_mirror_unhide_lagging

async_mirror_import
log_must wait_async_mirror_resilvered
log_must test "$digests" == "$(async_mirror_digests a 2)
$(async_mirror_digests b 2)"
async_mirror_clear_import_errors $ASYNC_MIRROR_LAGGING
log_must verify_pool $TESTPOOL
log_must check_pool_status $TESTPOOL errors "No known data errors"

log_pass "With the async mirror off, the pool behaves as a plain mirror."
