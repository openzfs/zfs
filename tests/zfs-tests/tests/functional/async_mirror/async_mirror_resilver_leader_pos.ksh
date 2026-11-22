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
#	When the lagging child had copied everything, was imported alone
#	and written, and the leader returns, nothing is lost: the leader is
#	resilvered from the lagging child.  The import sets the lagging child to
#	write_mode=sync; after the resilver the administrator sets
#	write_mode=behind again, and the same leader leads.  The feature stays
#	active meanwhile.
#
# STRATEGY:
#	1. Create a pool with a two-way mirror and turn the async mirror on.
#	2. Write files a, wait until the lagging child has caught up, and
#	   export the pool.
#	3. Hide the leader, import the lagging child alone, write files b,
#	   and export the pool.
#	4. Bring the leader back and import the pool.  Verify that the async mirror
#	   is off, that the lagging child has write_mode=sync, and that the
#	   feature is active.  Wait for the resilver.
#	5. Import the leader alone, read-only.  Verify files a and b, with no
#	   errors.
#	6. Import the pool.  Verify that the async mirror is still off, set
#	   write_mode=behind on the lagging child, verify that the async mirror
#	   is on with the same leader, write files c, wait
#	   until the lagging child has caught up, and verify all files.
#

verify_runnable "global"

function cleanup
{
	[[ -e $ASYNC_MIRROR_HIDDEN ]] && async_mirror_unhide_leader
	[[ -e $ASYNC_MIRROR_LAGGING_HIDDEN ]] && async_mirror_unhide_lagging
	cleanup_async_mirror
}

log_onexit cleanup

async_mirror_create_pool
async_mirror_enable

async_mirror_write_files a 2
log_must wait_async_mirror_idle
log_must zpool export $TESTPOOL

async_mirror_import_lagging
async_mirror_write_files b 2
typeset digests="$(async_mirror_digests a 2)
$(async_mirror_digests b 2)"
log_must zpool export $TESTPOOL
async_mirror_unhide_leader

async_mirror_import
log_must test "$(get_async_mirror_stat active)" == 0
log_must test \
    "$(get_async_mirror_prop write_mode $ASYNC_MIRROR_LAGGING)" == sync
log_must test "$(get_async_mirror_feature)" == "active"
log_must wait_async_mirror_resilvered
log_must zpool export $TESTPOOL

log_must mv $ASYNC_MIRROR_LAGGING $ASYNC_MIRROR_LAGGING_HIDDEN
log_must zpool import -o readonly=on -d $ASYNC_MIRROR_DIR $TESTPOOL
log_must test "$digests" == "$(async_mirror_digests a 2)
$(async_mirror_digests b 2)"
log_must check_pool_status $TESTPOOL errors "No known data errors"
log_must zpool export $TESTPOOL
async_mirror_unhide_lagging

async_mirror_import
log_must test "$(get_async_mirror_stat active)" == 0
log_must zpool set write_mode=behind $TESTPOOL $ASYNC_MIRROR_LAGGING
log_must wait_async_mirror_state "behind|current"
log_must test "$(get_async_mirror_stat active)" == 1
log_must test \
    "$(get_async_mirror_prop write_mode $ASYNC_MIRROR_LEADER)" == sync
async_mirror_write_files c 2
log_must wait_async_mirror_idle
log_must test "$digests
$(async_mirror_digests c 2)" == "$(async_mirror_digests a 2)
$(async_mirror_digests b 2)
$(async_mirror_digests c 2)"
log_must verify_pool $TESTPOOL

log_pass "A leader that the lagging child moved past is resilvered and " \
    "leads again."
