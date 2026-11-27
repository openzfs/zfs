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
#	write_mode is stored in the pool.  An import with no
#	tunable turns the async mirror on again, and after "write_mode=sync" an
#	import leaves it off.
#
# STRATEGY:
#	1. Create a pool with a two-way mirror and turn the async mirror on.
#	2. Export and import the pool.  Verify the property, the state and
#	   the kstat: the async mirror is on.
#	3. Write files a, and wait until the lagging child has caught up.
#	4. Set the property off, and wait until the state is off.
#	5. Export and import the pool.  Verify that the property, the state
#	   and the kstat are off, and that the feature returns to enabled.
#	6. Verify files a, and that a scrub finds no errors.
#

verify_runnable "global"

log_onexit cleanup_async_mirror

async_mirror_create_pool
async_mirror_enable

log_must zpool export $TESTPOOL
async_mirror_import
log_must test "$(get_async_mirror_prop)" == behind
log_must wait_async_mirror_state "behind|current"
log_must test "$(get_async_mirror_stat active)" -eq 1

async_mirror_write_files a 2
typeset digests="$(async_mirror_digests a 2)"
log_must wait_async_mirror_idle

log_must zpool set write_mode=sync $TESTPOOL $ASYNC_MIRROR_LAGGING
log_must wait_async_mirror_state off

log_must zpool export $TESTPOOL
async_mirror_import
log_must test "$(get_async_mirror_prop)" == sync
log_must test "$(get_async_mirror_prop state)" == off
log_must test "$(get_async_mirror_stat active)" -eq 0
log_must wait_async_mirror_feature enabled

log_must test "$digests" == "$(async_mirror_digests a 2)"
log_must verify_pool $TESTPOOL
log_must check_pool_status $TESTPOOL errors "No known data errors"

log_pass "write_mode persists across export and import."
