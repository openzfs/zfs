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
#	The async_mirror feature returns to the enabled state after the async mirror
#	is turned off and the pool is imported.
#
# STRATEGY:
#	1. Create a pool with a two-way mirror and turn the async mirror on.
#	2. Write some data, and verify that the feature becomes active.
#	3. Turn the async mirror off with an export and an import.
#	4. Verify that the feature returns to the enabled state.
#	5. Verify that the kstat shows no lagging child.
#	6. Verify the data and the state of the pool.
#

verify_runnable "global"

log_onexit cleanup_async_mirror

async_mirror_create_pool
async_mirror_enable

async_mirror_write_files file 4
typeset digests="$(async_mirror_digests file 4)"
log_must wait_async_mirror_feature active
log_must wait_async_mirror_idle

async_mirror_disable

log_must wait_async_mirror_feature enabled
log_must test "$(get_async_mirror_stat active)" -eq 0

log_must test "$digests" == "$(async_mirror_digests file 4)"
log_must check_state $TESTPOOL "" online
log_must verify_pool $TESTPOOL

log_pass "The feature returns to the enabled state."
