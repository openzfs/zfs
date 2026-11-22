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
#	An import that turns the async mirror off, and claims an intent log written
#	while the async mirror was on, resilvers that log to the lagging child.
#	The log blocks never reached the lagging child, and they are newer
#	than the txg that the import selects, so the DTL that ends the async mirror
#	does not reach them.  After the resilver, the lagging child alone
#	replays the same synchronous writes.  See
#	async_mirror_claimed_zil_copied_pos for
#	the import that keeps the async mirror on.
#
#	The lagging child must still drop writes when the property is
#	already off: a frozen child does, and it ends the freeze only once
#	the freeze is recorded, which the stopped release prevents.
#
# STRATEGY:
#	1. Create a pool with a two-way mirror and turn the async mirror on.  Create
#	   the intent log with one synchronous write, and wait until the
#	   lagging child has caught up.
#	2. Stop the release.  Freeze the lagging child: with the copy
#	   stopped, write file b, export and import, inject read errors on
#	   the indirect block of b, and restart the copy with no tries.
#	   Clear the injection.
#	3. Set the property of the lagging child to off: it stays frozen.
#	   Freeze the pool.  Write a file with synchronous writes: only the
#	   intent log on the leader has it.  Export.
#	4. Restart the release, and import without mounting: the import
#	   turns the async mirror off, claims the log, and nothing replays it.  Wait
#	   for the resilver.  Export.
#	5. Import the lagging child alone, which mounts the file system and
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

async_mirror_suspend_release 1
log_must async_mirror_set_tunable32 ASYNC_MIRROR_READ_RETRIES 0
async_mirror_suspend_copy 1
async_mirror_write /$TESTPOOL/b 16
sync_pool $TESTPOOL
typeset digest_b=$(xxh128digest /$TESTPOOL/b)
log_must zpool export $TESTPOOL
async_mirror_import
log_must zinject -a -t data -e io -T read -l 1 /$TESTPOOL/b
async_mirror_suspend_copy 0
log_must wait_async_mirror_frozen
log_must zinject -c all

log_must zpool set write_mode=sync $TESTPOOL $ASYNC_MIRROR_LAGGING
sync_pool $TESTPOOL
log_must test "$(get_async_mirror_prop state)" == frozen
log_must zpool freeze $TESTPOOL
log_must dd if=/dev/urandom of=/$TESTPOOL/payload oflag=sync bs=1k count=8
typeset digest=$(xxh128digest /$TESTPOOL/payload)
log_must zpool export $TESTPOOL

async_mirror_suspend_release 0
log_must zpool import -N -d $ASYNC_MIRROR_DIR $TESTPOOL
log_must test "$(get_async_mirror_stat active)" == 0
log_must wait_async_mirror_resilvered
log_must zpool export $TESTPOOL

async_mirror_import_lagging
log_must test -f /$TESTPOOL/payload
log_must test "$digest" == "$(xxh128digest /$TESTPOOL/payload)"
log_must test "$digest_b" == "$(xxh128digest /$TESTPOOL/b)"
log_must zpool scrub -w $TESTPOOL
log_must check_pool_status $TESTPOOL errors "No known data errors"
log_must zpool export $TESTPOOL
async_mirror_unhide_leader

log_pass "An import that ends the async mirror resilvers a claimed intent log."
