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
#	A pool with a backlog on the lagging child resumes the replay when it
#	is exported and imported again.
#
# STRATEGY:
#	1. Create a pool with a two-way mirror and turn the async mirror on.
#	2. Delay the I/O of the lagging child, to build a backlog.
#	3. Write several files, each one in a txg of its own, and verify
#	   that some txgs wait to be copied.
#	4. Remove the delay, and export the pool.
#	5. Import the pool with the async mirror still on.
#	6. Verify that the replay resumed: the async mirror is in use and the lagging
#	   child is not marked for a resilver.
#	7. Write more data, and wait until the lagging child has caught up.
#	8. Verify the data of all the files.
#

verify_runnable "global"

log_onexit cleanup_async_mirror

async_mirror_create_pool
async_mirror_enable

typeset guid=$(get_leaf_guid $ASYNC_MIRROR_LAGGING)

log_must zinject -d $guid -D 100:1 $TESTPOOL
async_mirror_write_files file 8
typeset digests="$(async_mirror_digests file 8)"
log_must test "$(get_async_mirror_stat pending_txgs)" -gt 0
log_must zinject -c all

log_must zpool export $TESTPOOL
async_mirror_import

log_must test "$(get_async_mirror_stat active)" -eq 1
log_must test "$(get_async_mirror_stat resync_from)" -eq 0

async_mirror_write_files more 2
log_must wait_async_mirror_idle
log_must test "$(get_async_mirror_stat failed)" -eq 0

log_must test "$digests" == "$(async_mirror_digests file 8)"
log_must verify_pool $TESTPOOL

log_pass "The replay resumes after an export and an import."
