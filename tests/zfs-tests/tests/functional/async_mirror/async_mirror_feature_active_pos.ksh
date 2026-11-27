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
#	Turning the async mirror on makes the async_mirror
#	feature active, and the kstat of the pool shows that the lagging
#	child is in use.
#
# STRATEGY:
#	1. Create a pool with a two-way mirror.
#	2. Verify that the async_mirror feature is enabled and that the
#	   kstat shows no lagging child.
#	3. Set one child as the lagging child.  The change takes effect live.
#	4. Write some data to the pool.
#	5. Verify that the feature becomes active.
#	6. Verify that the kstat shows a lagging child and no failure.
#

verify_runnable "global"

log_onexit cleanup_async_mirror

async_mirror_create_pool

log_must test "enabled" == "$(get_async_mirror_feature)"
log_must test "$(get_async_mirror_stat active)" -eq 0

async_mirror_enable

async_mirror_write_files file 2
log_must wait_async_mirror_feature active

log_must test "$(get_async_mirror_stat active)" -eq 1
log_must test "$(get_async_mirror_stat failed)" -eq 0

log_pass "The async mirror makes the feature active."
