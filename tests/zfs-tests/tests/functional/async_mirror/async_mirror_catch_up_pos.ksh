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
#	The lagging child catches up: the txg that the worker has applied
#	reaches the newest txg that has synced, and the backlog is empty.
#
# STRATEGY:
#	1. Create a pool with a two-way mirror and turn the async mirror on.
#	2. Write several files, each one in a txg of its own.
#	3. Read the newest txg that has synced from the kstat.
#	4. Wait until the applied txg reaches it.
#	5. Wait until no txg is pending.
#	6. Verify that the worker copied data and that it did not fail.
#

verify_runnable "global"

log_onexit cleanup_async_mirror

async_mirror_create_pool
async_mirror_enable

async_mirror_write_files file 8

typeset -i synced=$(get_async_mirror_stat synced_txg)

log_must test $synced -gt 0
log_must wait_async_mirror_applied $synced
log_must wait_async_mirror_idle

log_must test "$(get_async_mirror_stat applied_txg)" -ge $synced
log_must test "$(get_async_mirror_stat pending_bytes)" -eq 0
log_must test "$(get_async_mirror_stat copied_bytes)" -gt 0
log_must test "$(get_async_mirror_stat failed)" -eq 0

log_pass "The lagging child catches up with the leader."
