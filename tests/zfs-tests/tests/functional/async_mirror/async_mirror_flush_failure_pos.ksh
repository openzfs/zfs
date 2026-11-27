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
#	A failed cache flush of the lagging child stops the replay.  The
#	flush that follows the block writes must succeed before the
#	uberblock is written and the held space is released.
#
# STRATEGY:
#	1. Create a pool with a two-way mirror and turn the async mirror on.
#	2. Inject flush errors on the lagging child.
#	3. Write some data and wait until the replay fails.
#	4. Clear the injection and verify the pool with a scrub.
#

verify_runnable "global"

log_onexit cleanup_async_mirror

async_mirror_create_pool
async_mirror_enable

typeset guid=$(get_leaf_guid $ASYNC_MIRROR_LAGGING)
log_must zinject -d $guid -e io -T flush $TESTPOOL
async_mirror_write_files file 2
log_must wait_async_mirror_failed
log_must zinject -c all
log_must test "$(get_async_mirror_stat failed)" -eq 1
log_must wait_async_mirror_resilvered
log_must verify_pool $TESTPOOL

log_pass "A failed cache flush stops the replay."
