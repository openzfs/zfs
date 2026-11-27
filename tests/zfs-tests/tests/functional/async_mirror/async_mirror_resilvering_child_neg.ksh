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
#	A child that still needs a resilver does not become the lagging
#	child.  Its repair writes would be lost.
#
# STRATEGY:
#	1. Create a pool with a two-way mirror and write some data.
#	2. Stop the progress of scans, so that the resilver cannot end.
#	   Offline the child, write more data, and online it.
#	3. Verify that "zpool set write_mode=behind" is refused, and
#	   that the async mirror is not active.
#	4. Let scans progress, wait for the resilver, and verify the pool.
#

verify_runnable "global"

log_onexit cleanup_async_mirror

async_mirror_create_pool
async_mirror_write_files file 2

#
# The resilver after the online is small.  Without the stop it can end
# before the set, and the child then rightly becomes the lagging child.
#
log_must async_mirror_set_tunable32 SCAN_SUSPEND_PROGRESS 1
log_must zpool offline $TESTPOOL $ASYNC_MIRROR_LAGGING
async_mirror_write_files more 2
typeset digests="$(async_mirror_digests more 2)"
log_must zpool online $TESTPOOL $ASYNC_MIRROR_LAGGING

log_mustnot zpool set write_mode=behind $TESTPOOL $ASYNC_MIRROR_LAGGING
log_must test "$(get_async_mirror_prop)" == sync
log_must test "$(get_async_mirror_stat active)" -eq 0

log_must async_mirror_set_tunable32 SCAN_SUSPEND_PROGRESS 0
log_must wait_async_mirror_resilvered
log_must test "$digests" == "$(async_mirror_digests more 2)"
log_must verify_pool $TESTPOOL

log_pass "A child that needs a resilver does not become the lagging child."
