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
#	Nothing is released while the pool is frozen.  A txg after
#	"zpool freeze" never reaches the disk, so a release in it would free
#	held ranges in memory only.  The allocator could put intent log
#	blocks there, and the next import could not claim them, because
#	the ranges are still held on disk: the replay would miss records.
#
# STRATEGY:
#	1. Create a pool with a two-way mirror, and turn the async mirror on.
#	2. Stop the copy.  Write files and remove them, so that their frees
#	   are held by records that wait to be copied.
#	3. Freeze the pool, and restart the copy.  Wait until the copy has
#	   passed every record, and sync a few txgs.  Verify that the held
#	   space did not shrink.
#	4. Write a file with synchronous writes: only the intent log has it.
#	   Export, import, and verify the file.
#

verify_runnable "global"

log_onexit cleanup_async_mirror

async_mirror_create_pool
async_mirror_enable
log_must dd if=/dev/zero of=/$TESTPOOL/sync conv=fdatasync,fsync bs=1 count=1
log_must wait_async_mirror_idle

async_mirror_suspend_copy 1
async_mirror_write_files x 8
log_must rm /$TESTPOOL/x.*
sync_pool $TESTPOOL
sync_pool $TESTPOOL
typeset -i held=$(get_async_mirror_stat held_bytes)
log_note "held bytes before the freeze: $held"
log_must test $held -gt 0

log_must zpool freeze $TESTPOOL
async_mirror_suspend_copy 0
log_must wait_async_mirror_stat pending_txgs -eq 0
typeset -i i
for (( i = 0; i < 4; i++ )); do
	sync_pool $TESTPOOL
done
typeset -i after=$(get_async_mirror_stat held_bytes)
log_note "held bytes while frozen: $after"
log_must test $after -ge $held

log_must dd if=/dev/urandom of=/$TESTPOOL/payload oflag=sync bs=4k count=64
typeset digest=$(xxh128digest /$TESTPOOL/payload)
log_must zpool export $TESTPOOL
async_mirror_import
log_must test "$digest" == "$(xxh128digest /$TESTPOOL/payload)"
log_must verify_pool $TESTPOOL

log_pass "Nothing is released while the pool is frozen."
