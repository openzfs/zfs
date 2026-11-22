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
#	A read-only import of the lagging child alone does not fork the
#	pool.  When the leader returns, the pool imports, and the copy to
#	the lagging child resumes.
#
# STRATEGY:
#	1. Create a pool with a two-way mirror and turn the async mirror on.
#	2. Write files a, and wait until the lagging child has caught up.
#	3. Stop the copy, write files b, and export the pool.
#	4. Hide the leader, import the lagging child alone read-only, and
#	   export it.
#	5. Bring the leader back, restart the copy, and import the pool.
#	6. Verify that the async mirror is on, wait until the lagging child has
#	   caught up, and verify files a and b.
#

verify_runnable "global"

log_onexit cleanup_async_mirror

async_mirror_create_pool
async_mirror_enable

async_mirror_write_files a 2
log_must wait_async_mirror_idle
typeset digests="$(async_mirror_digests a 2)"
async_mirror_export_behind b 2

log_must mv $ASYNC_MIRROR_LEADER $ASYNC_MIRROR_HIDDEN
log_must zpool import -o readonly=on -d $ASYNC_MIRROR_DIR $TESTPOOL
log_must zpool export $TESTPOOL
async_mirror_unhide_leader

async_mirror_suspend_copy 0
async_mirror_import
log_must test "$(get_async_mirror_stat active)" == 1
log_must wait_async_mirror_idle
log_must test "$digests" == "$(async_mirror_digests a 2)"
log_must test -e /$TESTPOOL/b.1

log_pass "A read-only import of the lagging child alone forks nothing."
