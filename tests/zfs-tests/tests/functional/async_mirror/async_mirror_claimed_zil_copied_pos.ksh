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
#	An intent log that an import claims reaches the lagging child.  From
#	the claim txg on, the log is part of the pool: the replay at mount
#	puts its writes in the tree.  So the lagging child, imported alone
#	at a txg after the claim, replays the same synchronous writes.
#
# STRATEGY:
#	1. Create a pool with a two-way mirror and turn the async mirror on.  Create
#	   the intent log with one synchronous write, and wait until the
#	   lagging child has caught up.
#	2. Stop the copy and freeze the pool.  Write a file with small
#	   synchronous writes, which the log holds, and one with large ones,
#	   whose data blocks the log points to: only the leader has them.
#	   Export.
#	3. Restart the copy and import without mounting: the import claims
#	   the log, and nothing replays it.  Wait until the lagging child has
#	   caught up, past the claim txg.  Export.
#	4. Import the lagging child alone, which mounts the file system and
#	   replays the log.  Verify the files, and that a scrub finds no
#	   error.
#

verify_runnable "global"

function cleanup
{
	[[ -e $ASYNC_MIRROR_HIDDEN ]] && async_mirror_unhide_leader
	cleanup_async_mirror
}

log_onexit cleanup

async_mirror_create_pool
async_mirror_enable
log_must dd if=/dev/zero of=/$TESTPOOL/sync conv=fdatasync,fsync bs=1 count=1
log_must wait_async_mirror_idle

async_mirror_suspend_copy 1
log_must zpool freeze $TESTPOOL
log_must dd if=/dev/urandom of=/$TESTPOOL/payload oflag=sync bs=1k count=8
typeset digest=$(xxh128digest /$TESTPOOL/payload)
log_must dd if=/dev/urandom of=/$TESTPOOL/indirect oflag=sync bs=128k count=4
typeset digest2=$(xxh128digest /$TESTPOOL/indirect)
log_must zpool export $TESTPOOL

async_mirror_suspend_copy 0
log_must zpool import -N -d $ASYNC_MIRROR_DIR $TESTPOOL
log_must test "$(get_async_mirror_stat active)" == 1
log_must wait_async_mirror_idle
log_must test "$(get_async_mirror_stat failed)" == 0
log_must zpool export $TESTPOOL

async_mirror_import_lagging
log_must test -f /$TESTPOOL/payload
log_must test "$digest" == "$(xxh128digest /$TESTPOOL/payload)"
log_must test "$digest2" == "$(xxh128digest /$TESTPOOL/indirect)"
log_must zpool scrub -w $TESTPOOL
log_must check_pool_status $TESTPOOL errors "No known data errors"
log_must zpool export $TESTPOOL
async_mirror_unhide_leader

log_pass "A claimed intent log reaches the lagging child."
