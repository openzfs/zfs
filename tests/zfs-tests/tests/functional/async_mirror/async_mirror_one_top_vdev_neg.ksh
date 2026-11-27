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
#	The async mirror needs one top-level vdev with data.  A log device is
#	allowed.  Another data vdev cannot be present or be added.
#
# STRATEGY:
#	1. Create a pool with a mirror and a second data vdev.  Verify
#	   that "zpool set write_mode=behind" is refused, and that the
#	   async mirror does not start.
#	2. Create a pool with a mirror and a log device.  Verify that
#	   the async mirror turns on.
#	3. Verify that a data vdev cannot be added, and a log device can.
#

verify_runnable "global"

log_onexit cleanup_async_mirror

async_mirror_create_pool $ASYNC_MIRROR_EXTRA
log_mustnot zpool set write_mode=behind $TESTPOOL $ASYNC_MIRROR_LAGGING
log_must test "$(get_async_mirror_prop)" == sync
log_must test "$(get_async_mirror_stat active)" -eq 0
log_must destroy_pool $TESTPOOL

async_mirror_create_pool
async_mirror_enable
log_must test "$(get_async_mirror_stat active)" -eq 1

log_must truncate -s $ASYNC_MIRROR_VDEV_SIZE $ASYNC_MIRROR_EXTRA
log_mustnot zpool add -f $TESTPOOL $ASYNC_MIRROR_EXTRA
log_must zpool add -f $TESTPOOL log $ASYNC_MIRROR_EXTRA
log_must test "$(get_async_mirror_stat active)" -eq 1

log_pass "The async mirror needs one top-level vdev with data."
