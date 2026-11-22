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
#	The lagging child alone imports when its txg has an intent log that
#	no import claimed.  It does not hold the log blocks, which are newer
#	than its txg, so the log ends at a checksum error and reads as
#	empty.  The read fence must not turn that into ENXIO, which fails
#	the import.
#
# STRATEGY:
#	1. Create a pool with a two-way mirror and turn the async mirror on.  Write
#	   files a, create the intent log with one synchronous write, and
#	   wait until the lagging child has caught up.
#	2. Stop the copy and freeze the pool.  Write file b with synchronous
#	   writes, so that only the log on the leader has it.  Export.
#	3. Import the lagging child alone.  Verify that the import works,
#	   that files a are intact, and that b does not exist.
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
async_mirror_write_files a 2
typeset digests="$(async_mirror_digests a 2)"
log_must dd if=/dev/zero of=/$TESTPOOL/sync conv=fdatasync,fsync bs=1 count=1
log_must wait_async_mirror_idle

async_mirror_suspend_copy 1
log_must zpool freeze $TESTPOOL
log_must dd if=/dev/urandom of=/$TESTPOOL/b oflag=sync bs=1k count=8
log_must zpool export $TESTPOOL

async_mirror_import_lagging
log_must test "$digests" == "$(async_mirror_digests a 2)"
log_mustnot test -e /$TESTPOOL/b
log_must zpool export $TESTPOOL
async_mirror_unhide_leader

log_pass "The lagging child alone imports with a log that it lacks."
