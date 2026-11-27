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
#	A forced import resumes an async mirror after an export with an
#	unapplied copy backlog.
#
# STRATEGY:
#	1. Create an async mirror and leave its lagging child behind at export.
#	2. Import with -F and verify the pool data and async mirror state.
#	3. Repeat with -F -X and verify the pool data and async mirror state.
#	4. Wait for each imported mirror to copy its backlog.
#

verify_runnable "global"

log_onexit cleanup_async_mirror
log_assert "Forced imports restore async mirror backlog data."

async_mirror_create_pool
async_mirror_enable

async_mirror_suspend_copy 1
async_mirror_write_files forced 2
typeset forced_digests="$(async_mirror_digests forced 2)"
log_must test "$(get_async_mirror_stat pending_txgs)" -gt 0
log_must zpool export $TESTPOOL
log_must zpool import -F -d $ASYNC_MIRROR_DIR $TESTPOOL
log_must test "$forced_digests" == "$(async_mirror_digests forced 2)"
log_must test "$(get_async_mirror_stat active)" -eq 1
log_must wait_async_mirror_state "behind|current"
async_mirror_suspend_copy 0
log_must wait_async_mirror_idle 600

async_mirror_suspend_copy 1
async_mirror_write_files extreme 2
typeset extreme_digests="$(async_mirror_digests extreme 2)"
log_must test "$(get_async_mirror_stat pending_txgs)" -gt 0
log_must zpool export $TESTPOOL
log_must zpool import -F -X -d $ASYNC_MIRROR_DIR $TESTPOOL
log_must test "$extreme_digests" == "$(async_mirror_digests extreme 2)"
log_must test "$(get_async_mirror_stat active)" -eq 1
log_must wait_async_mirror_state "behind|current"
async_mirror_suspend_copy 0
log_must wait_async_mirror_idle 600

log_pass "Forced imports resume an async mirror with a copy backlog."
