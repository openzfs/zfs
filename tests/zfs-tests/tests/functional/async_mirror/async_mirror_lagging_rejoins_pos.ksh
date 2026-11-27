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
#	When the leader was imported alone and written, and the lagging
#	child returns without writes of its own, the pool imports and the
#	lagging child is resilvered.
#
# STRATEGY:
#	1. Create a pool with a two-way mirror and turn the async mirror on.
#	2. Write files a, wait until the lagging child has caught up, and
#	   export the pool.
#	3. Hide the lagging child, import the leader alone, write files b,
#	   and export the pool.
#	4. Bring the lagging child back and import the pool.
#	5. Wait for the resilver.  Verify files a and b, and that a scrub
#	   finds no errors.
#

verify_runnable "global"

log_onexit cleanup_async_mirror

async_mirror_create_pool
async_mirror_enable

async_mirror_write_files a 2
log_must wait_async_mirror_idle
log_must zpool export $TESTPOOL

async_mirror_import_leader
async_mirror_write_files b 2
typeset digests="$(async_mirror_digests a 2)
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

log_pass "A lagging child with no writes of its own rejoins the pool."
