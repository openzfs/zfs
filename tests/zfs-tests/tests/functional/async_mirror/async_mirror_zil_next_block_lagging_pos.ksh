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
#	A crash with a live intent log chain that ends in a log block
#	allocated ahead of its write leaves the lagging child a consistent
#	pool at its applied txg.  ZFS allocates the next log block when it
#	issues the one before, and writes it later, in place.  So the chain
#	on the leader ends in a block that the pool allocated and that the
#	copy has no record of.  The lagging child alone must not hold any
#	of the chain, which is newer than its txg, and must not fail on the
#	range of a block that was never written to it.
#
# STRATEGY:
#	1. Create a pool with a two-way mirror and turn the async mirror on.
#	   Write files a, create the intent log with one synchronous write,
#	   and wait until the lagging child has caught up.
#	2. Stop the copy and freeze the pool.  Write files b with many
#	   synchronous writes: small ones make a chain of log blocks,
#	   a large one makes a write record that points to a data block.
#	   Sync the pool once in the middle of the writes, so that the
#	   chain survives a txg, with its last block allocated and not
#	   written.  Export, as after a crash.
#	3. Import the lagging child alone.  Verify files a, that no file b
#	   exists (a log block above the applied txg would replay one), and
#	   that a scrub finds no error on the lagging child.
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
typeset -i i
for (( i = 0; i < 6; i++ )); do
	log_must dd if=/dev/urandom of=/$TESTPOOL/b.$i oflag=sync bs=1k \
	    count=8
	(( i == 2 )) && log_must zpool sync -l $TESTPOOL
done
log_must dd if=/dev/urandom of=/$TESTPOOL/b.big oflag=sync bs=128k count=4
log_must zpool export $TESTPOOL
async_mirror_note_zil

async_mirror_import_lagging
log_must test "$digests" == "$(async_mirror_digests a 2)"
for (( i = 0; i < 6; i++ )); do
	log_mustnot test -e /$TESTPOOL/b.$i
done
log_mustnot test -e /$TESTPOOL/b.big
async_mirror_check_clean $ASYNC_MIRROR_LAGGING
log_must zpool export $TESTPOOL
async_mirror_unhide_leader

log_pass "The lagging child alone is clean with an unwritten next log block."
