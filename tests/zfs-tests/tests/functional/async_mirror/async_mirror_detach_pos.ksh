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
#	A child cannot be detached while the async mirror is on.  The operator
#	turns the async mirror off first, and then the lagging child can leave
#	the pool.
#
# STRATEGY:
#	1. Create a pool with a two-way mirror and turn the async mirror on.
#	2. Write several files, and record a digest of each file.
#	3. Wait until the lagging child has caught up.
#	4. Verify that detach of either child fails, and that the error
#	   names the async mirror.
#	5. Turn the async mirror off with an export and an import.
#	6. Verify that the feature returns to the enabled state.
#	7. Detach the lagging child.
#	8. Verify that the child has left the pool and that the pool is
#	   online with the same data, and that a scrub finds no errors.
#

verify_runnable "global"

log_onexit cleanup_async_mirror

async_mirror_create_pool
async_mirror_enable

async_mirror_write_files file 4
typeset digests="$(async_mirror_digests file 4)"
log_must wait_async_mirror_idle

log_mustnot zpool detach $TESTPOOL $ASYNC_MIRROR_LAGGING
log_mustnot zpool detach $TESTPOOL $ASYNC_MIRROR_LEADER
log_must eval "zpool detach $TESTPOOL $ASYNC_MIRROR_LAGGING 2>&1 |
    grep -q asynchronous"

async_mirror_disable
log_must wait_async_mirror_feature enabled

log_must zpool detach $TESTPOOL $ASYNC_MIRROR_LAGGING
log_mustnot eval "zpool status $TESTPOOL | grep -q $ASYNC_MIRROR_LAGGING"
log_must check_state $TESTPOOL "" online
log_must test "$digests" == "$(async_mirror_digests file 4)"
log_must verify_pool $TESTPOOL

log_pass "A child can only be detached after the async mirror is turned off."
