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
#	After a refused import of copies that were written separately, the
#	operator can inspect each copy read-only, keep one, and add the
#	other device back.  The async mirror can then be turned on again.
#
# STRATEGY:
#	1. Create a pool with a two-way mirror and turn the async mirror on.
#	2. Write files a, and wait until the lagging child has caught up.
#	3. Stop the copy, write files b, and export the pool.
#	4. Hide the leader.  Import the lagging child alone, write file c,
#	   and export the pool.  Bring the leader back.
#	5. Verify that a read-write import is refused, and that a read-only
#	   import works.
#	6. Hide the leader, import the lagging child alone read-only, and
#	   verify file c.  Bring the leader back.
#	7. Keep the leader: hide the lagging child, import the leader alone,
#	   clear the label of the lagging child, and replace it.
#	8. Wait for the resilver.  Verify files a and b, that file c is
#	   gone, and that a scrub finds no errors.
#	9. Turn the async mirror on again, and verify that it is on.
#

verify_runnable "global"

log_onexit cleanup_async_mirror

async_mirror_create_pool
async_mirror_enable

async_mirror_write_files a 2
log_must wait_async_mirror_idle
typeset digests="$(async_mirror_digests a 2)"
async_mirror_export_behind b 2

async_mirror_suspend_copy 0
async_mirror_import_lagging
async_mirror_write /$TESTPOOL/c 4
typeset cdigest=$(xxh128digest /$TESTPOOL/c)
log_must zpool export $TESTPOOL
async_mirror_unhide_leader

log_mustnot zpool import -d $ASYNC_MIRROR_DIR $TESTPOOL
log_must zpool import -o readonly=on -d $ASYNC_MIRROR_DIR $TESTPOOL
log_must zpool export $TESTPOOL

log_must mv $ASYNC_MIRROR_LEADER $ASYNC_MIRROR_HIDDEN
log_must zpool import -o readonly=on -d $ASYNC_MIRROR_DIR $TESTPOOL
log_must test "$cdigest" == "$(xxh128digest /$TESTPOOL/c)"
log_must zpool export $TESTPOOL
async_mirror_unhide_leader

typeset guid=$(get_leaf_guid $ASYNC_MIRROR_LAGGING)
async_mirror_import_leader
log_must zpool labelclear -f $ASYNC_MIRROR_LAGGING_HIDDEN
async_mirror_unhide_lagging
log_must zpool replace -f $TESTPOOL $guid $ASYNC_MIRROR_LAGGING
log_must wait_async_mirror_resilvered

log_must test "$digests" == "$(async_mirror_digests a 2)"
log_must test -e /$TESTPOOL/b.1
log_mustnot test -e /$TESTPOOL/c
log_must verify_pool $TESTPOOL
log_must check_pool_status $TESTPOOL errors "No known data errors"

async_mirror_enable
log_must test "$(get_async_mirror_stat active)" == 1

log_pass "The operator can keep one copy and turn the async mirror on again."
