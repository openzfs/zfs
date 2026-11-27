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
#	The lagging child, imported alone, holds the data of the pool and
#	passes a scrub without errors.
#
# STRATEGY:
#	1. Create a pool with a two-way mirror and turn the async mirror on.
#	2. Write several files, and record a digest of each file.
#	3. Wait until the lagging child has caught up.
#	4. Export the pool.
#	5. Import the pool from the lagging child only.
#	6. Verify that every file has the same digest as before.
#	7. Scrub the pool, and verify that the scrub finds no errors.
#

verify_runnable "global"

log_onexit cleanup_async_mirror

async_mirror_create_pool
async_mirror_enable

async_mirror_write_files file 6
typeset digests="$(async_mirror_digests file 6)"
log_must wait_async_mirror_idle

log_must zpool export $TESTPOOL
async_mirror_import_lagging

log_must test "$digests" == "$(async_mirror_digests file 6)"
log_must verify_pool $TESTPOOL
log_must check_pool_status $TESTPOOL errors "No known data errors"

log_pass "The lagging child alone has the data and a clean scrub."
