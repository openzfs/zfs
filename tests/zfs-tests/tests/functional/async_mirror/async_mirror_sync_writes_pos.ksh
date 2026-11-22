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
#	Synchronous writes of large blocks reach the lagging child.  The ZIL
#	writes such blocks with dmu_sync().  They are file data, and they
#	stay in the pool after the txg syncs.
#
# STRATEGY:
#	1. Create a pool with a two-way mirror and turn the async mirror on.
#	2. Set sync=always and write 128 KiB blocks.
#	3. Wait until the lagging child has caught up.
#	4. Export the pool and import the lagging child alone.
#	5. Verify the data, and that a scrub finds no errors.
#

verify_runnable "global"

log_onexit cleanup_async_mirror

async_mirror_create_pool
async_mirror_enable

log_must zfs set sync=always $TESTPOOL
async_mirror_write_files file 8
typeset digests="$(async_mirror_digests file 8)"
log_must wait_async_mirror_idle
log_must test "$(get_async_mirror_stat failed)" -eq 0

log_must zpool export $TESTPOOL
async_mirror_import_lagging

log_must test "$digests" == "$(async_mirror_digests file 8)"
log_must verify_pool $TESTPOOL
log_must check_pool_status $TESTPOOL errors "No known data errors"

log_pass "Synchronous writes reach the lagging child."
