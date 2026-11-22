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
#	A checkpoint and a reguid are refused while the async mirror is on.
#	The labels of the lagging child would keep the old pool guid.
#
# STRATEGY:
#	1. Create a pool with a two-way mirror and turn the async mirror on.
#	2. Verify that 'zpool checkpoint' fails and the pool has no checkpoint.
#	3. Verify that 'zpool reguid' fails and the guid does not change.
#	4. Turn the async mirror off and verify that 'zpool reguid' works.
#

verify_runnable "global"

log_onexit cleanup_async_mirror

async_mirror_create_pool
async_mirror_enable

typeset guid="$(get_pool_prop guid $TESTPOOL)"

log_mustnot zpool checkpoint $TESTPOOL
log_must test "$(get_pool_prop checkpoint $TESTPOOL)" == "-"

log_mustnot zpool reguid $TESTPOOL
log_must test "$guid" == "$(get_pool_prop guid $TESTPOOL)"

async_mirror_disable
log_must zpool reguid $TESTPOOL
log_mustnot test "$guid" == "$(get_pool_prop guid $TESTPOOL)"

log_pass "Checkpoint and reguid are refused while the async mirror is on."
