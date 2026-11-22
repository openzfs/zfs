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
#	A copy that was taken offline while the leader ran alone, and that
#	nothing wrote, comes back with "zpool online" and resilvers.  The
#	check of "zpool online" must not refuse a copy of the same history.
#
# STRATEGY:
#	1. Create a pool with a two-way mirror and turn the async mirror on.
#	2. Write files a, wait until the lagging child has caught up, and
#	   export the pool.
#	3. Hide the lagging child, import the leader alone, take the lagging
#	   child offline, write files c, and export the pool.  Bring the
#	   lagging child back.
#	4. Import both copies and verify that the lagging child is offline.
#	5. Bring the lagging child online, and wait for the resilver.
#	6. Verify files a and c, and that a scrub finds no errors.
#

verify_runnable "global"

log_onexit cleanup_async_mirror

async_mirror_create_pool
typeset guid=$(get_leaf_guid $ASYNC_MIRROR_LAGGING)
async_mirror_enable

async_mirror_write_files a 2
log_must wait_async_mirror_idle
log_must zpool export $TESTPOOL

async_mirror_import_leader
log_must zpool offline $TESTPOOL $guid
async_mirror_write_files c 2
typeset digests="$(async_mirror_digests a 2)
$(async_mirror_digests c 2)"
log_must zpool export $TESTPOOL
async_mirror_unhide_lagging

async_mirror_import
log_must check_state $TESTPOOL $ASYNC_MIRROR_LAGGING "OFFLINE"

log_must zpool online $TESTPOOL $ASYNC_MIRROR_LAGGING
log_must check_state $TESTPOOL $ASYNC_MIRROR_LAGGING "ONLINE"
log_must wait_async_mirror_resilvered

log_must test "$digests" == "$(async_mirror_digests a 2)
$(async_mirror_digests c 2)"
async_mirror_clear_import_errors $ASYNC_MIRROR_LAGGING
log_must verify_pool $TESTPOOL
log_must check_pool_status $TESTPOOL errors "No known data errors"

log_pass "zpool online accepts a copy of the same history."
