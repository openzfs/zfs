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
#	After a clean export, the lagging child is imported alone and
#	written, and the leader returns.  The leader is then an older
#	prefix of the pool, so the import works, but the leader needs a
#	resilver.  The async mirror must not start with it as the leader: it would
#	be the source of every copy and the only target of new writes, and
#	it lacks the newest txgs.  The import sets the lagging child to
#	write_mode=sync; the administrator sets write_mode=behind again after
#	the resilver.
#
# STRATEGY:
#	1. Create a pool with a two-way mirror and turn the async mirror on.
#	2. Write files a, wait until the lagging child has caught up, and
#	   export the pool.
#	3. Hide the leader.  Import the lagging child alone, write files b,
#	   and export the pool.  Bring the leader back.
#	4. Import the pool.  Verify that the async mirror is off and that the
#	   lagging child has write_mode=sync, and wait for the
#	   resilver.  Verify files a and b, and that a scrub finds no
#	   errors.
#	5. Export and import the pool, verify that the async mirror is still
#	   off, set write_mode=behind on the lagging child, and verify that
#	   the async mirror is on.
#

verify_runnable "global"

log_onexit cleanup_async_mirror

async_mirror_create_pool
async_mirror_enable

async_mirror_write_files a 2
log_must wait_async_mirror_idle
log_must zpool export $TESTPOOL

async_mirror_import_lagging
async_mirror_write_files b 2
typeset digests="$(async_mirror_digests a 2)
$(async_mirror_digests b 2)"
log_must zpool export $TESTPOOL
async_mirror_unhide_leader

async_mirror_import
log_must test "$(get_async_mirror_stat active)" == 0
log_must test \
    "$(get_async_mirror_prop write_mode $ASYNC_MIRROR_LAGGING)" == sync
log_must wait_async_mirror_resilvered
log_must test "$digests" == "$(async_mirror_digests a 2)
$(async_mirror_digests b 2)"
async_mirror_clear_import_errors $ASYNC_MIRROR_LEADER
log_must verify_pool $TESTPOOL
log_must check_pool_status $TESTPOOL errors "No known data errors"

log_must zpool export $TESTPOOL
async_mirror_import
log_must test "$(get_async_mirror_stat active)" == 0
log_must zpool set write_mode=behind $TESTPOOL $ASYNC_MIRROR_LAGGING
log_must wait_async_mirror_state "behind|current"
log_must test "$(get_async_mirror_stat active)" == 1

log_pass "A leader that needs a resilver does not start the async mirror."
