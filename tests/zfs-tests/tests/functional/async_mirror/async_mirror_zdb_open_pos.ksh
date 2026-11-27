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
#	zdb can open a pool that runs the async mirror.  The
#	labels of the lagging child keep the state they had when the async mirror
#	turned on, so the state can be stale.  An open from the cache file
#	must not refuse the pool for that reason.
#
# STRATEGY:
#	1. Create a pool with a two-way mirror, turn the async mirror on, and
#	   export and import the pool.
#	2. Write some data to the pool.
#	3. Verify that "zdb -u" and "zdb -C" open the pool, and that the
#	   txg of the uberblock is not zero.
#

verify_runnable "global"

log_onexit cleanup_async_mirror

async_mirror_create_pool
async_mirror_enable
log_must zpool export $TESTPOOL
async_mirror_import

async_mirror_write_files file 2
log_must wait_async_mirror_idle

log_must eval "zdb -u $TESTPOOL > /dev/null"
log_must eval "zdb -C $TESTPOOL > /dev/null"

typeset -i txg=$(get_async_mirror_pool_txg)
log_must test $txg -gt 0

log_pass "zdb opens a pool that runs the async mirror."
