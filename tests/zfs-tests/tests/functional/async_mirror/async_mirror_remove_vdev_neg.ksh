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
#	A top-level vdev with a lagging child cannot be removed.
#
# STRATEGY:
#	1. Create a pool with a two-way mirror and a log device.
#	2. Turn the async mirror on, and write some data.
#	3. Verify that removing the mirror fails.
#	4. Verify that the mirror is still in the pool.
#

verify_runnable "global"

log_onexit cleanup_async_mirror

async_mirror_create_pool log $ASYNC_MIRROR_EXTRA
async_mirror_enable

async_mirror_write_files file 2

log_mustnot zpool remove $TESTPOOL mirror-0
log_must eval "zpool status $TESTPOOL | grep -q mirror-0"
log_must check_state $TESTPOOL "" online

log_pass "A vdev with a lagging child cannot be removed."
