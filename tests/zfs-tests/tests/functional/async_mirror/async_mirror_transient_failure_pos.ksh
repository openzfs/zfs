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
#	A crash after a failed copy, before the failure is recorded in the
#	pool, does not give the lagging child a txg whose blocks it lacks.
#	Until the failure is on disk, each txg keeps a full record.  A
#	synchronous write issued before the failure is dropped for the
#	lagging child, and its txg syncs after the failure, so the record
#	of that txg must still list it.
#
# STRATEGY:
#	1. Create a pool with a two-way mirror and turn the async mirror on.  Write
#	   files a and wait until the lagging child has caught up.
#	2. Stop the copy and write file b, so that a txg waits to be
#	   copied.  Stop the release, so that the failure is never recorded
#	   in the pool, as after a crash.
#	3. Inject write errors on the lagging child.  Keep the txg open,
#	   set sync=always and write file c: dmu_sync() writes its blocks in
#	   the open txg.
#	4. Restart the copy and wait until it fails.  Clear the injection
#	   and sync the pool, so the txg of c syncs after the failure.
#	5. Export, restart the release, and import with the async mirror on.  The
#	   copy resumes.  Wait until the lagging child has caught up.
#	6. Export and import the lagging child alone.  Verify files a, b
#	   and c, and that a scrub finds no error.
#

verify_runnable "global"

typeset timeout=$(get_tunable TXG_TIMEOUT)

function cleanup
{
	log_must async_mirror_set_tunable32 TXG_TIMEOUT $timeout
	[[ -e $ASYNC_MIRROR_HIDDEN ]] && async_mirror_unhide_leader
	cleanup_async_mirror
}

log_onexit cleanup

async_mirror_create_pool
async_mirror_enable
async_mirror_write_files a 2
log_must wait_async_mirror_idle

async_mirror_suspend_copy 1
async_mirror_write /$TESTPOOL/b 16
sync_pool $TESTPOOL
log_must test "$(get_async_mirror_stat pending_txgs)" -gt 0
log_must async_mirror_set_tunable32 ASYNC_MIRROR_SUSPEND_RELEASE 1

typeset guid=$(get_leaf_guid $ASYNC_MIRROR_LAGGING)
log_must zinject -d $guid -e io -T write $TESTPOOL
log_must async_mirror_set_tunable32 TXG_TIMEOUT 600
log_must zfs set sync=always $TESTPOOL
async_mirror_write /$TESTPOOL/c 4
log_must zfs set sync=standard $TESTPOOL

async_mirror_suspend_copy 0
log_must wait_async_mirror_failed
log_must zinject -c all
log_must async_mirror_set_tunable32 TXG_TIMEOUT $timeout
sync_pool $TESTPOOL

typeset digests="$(async_mirror_digests a 2)
$(xxh128digest /$TESTPOOL/b)
$(xxh128digest /$TESTPOOL/c)"
log_must zpool export $TESTPOOL
log_must async_mirror_set_tunable32 ASYNC_MIRROR_SUSPEND_RELEASE 0

async_mirror_import
log_must wait_async_mirror_idle
log_must test "$(get_async_mirror_stat active)" == 1
log_must test "$(get_async_mirror_stat failed)" == 0
log_must zpool export $TESTPOOL

async_mirror_import_lagging
log_must test "$digests" == "$(async_mirror_digests a 2)
$(xxh128digest /$TESTPOOL/b)
$(xxh128digest /$TESTPOOL/c)"
log_must zpool scrub -w $TESTPOOL
log_must check_pool_status $TESTPOOL errors "No known data errors"
log_must zpool export $TESTPOOL
async_mirror_unhide_leader

log_pass "A failure that is not on disk keeps each txg's record complete."
