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
#	Turning the async mirror on while synchronous writes run leaves the
#	lagging child a consistent pool at its applied txg, after the log
#	blocks that were allocated before the switch are freed and their
#	ranges are used for data.  The free of a log block of this import
#	is not held, also when the block was allocated before the switch;
#	see vdev_async_mirror_should_hold().  The lagging child may hold
#	such a block, written before the switch, as the head of its chain.
#	When the copy reuses the range, the chain is cut.  A cut chain must
#	read as an end of the log, not as an error.
#
# STRATEGY:
#	1. Create a pool with a two-way mirror and a second file system,
#	   fs2.  Start a writer that makes one synchronous write of 4 KiB
#	   at a time, and turn the async mirror on while it runs.
#	2. Let the writer run for some txgs, so that the log blocks of the
#	   switch are freed.  Stop it.
#	3. Write much data to fs2, so that the allocator reuses the freed
#	   ranges, while a delay on the lagging child keeps the copy
#	   behind.  Stop the copy, freeze the pool, start the writer
#	   again for a new chain, and export, as after a crash.
#	4. Import the lagging child alone.  Verify that the file of the
#	   writer has only the bytes that it wrote, and that a scrub finds
#	   no error.
#

verify_runnable "global"

function cleanup
{
	async_mirror_stop_sync_writer
	[[ -e $ASYNC_MIRROR_HIDDEN ]] && async_mirror_unhide_leader
	cleanup_async_mirror
	rm -rf $FS2
}

log_onexit cleanup

# Outside the pool's file system, which is mounted after fs2.
FS2=$TEST_BASE_DIR/async_mirror_fs2
# The churn writes about half of the pool.
ASYNC_MIRROR_VDEV_SIZE=256m

async_mirror_create_pool
log_must zfs create -o mountpoint=$FS2 $TESTPOOL/fs2
async_mirror_write_files a 1
typeset digests="$(async_mirror_digests a 1)"

async_mirror_start_sync_writer /$TESTPOOL/w
async_mirror_enable
typeset -i i
for (( i = 0; i < 8; i++ )); do
	log_must zpool sync -l $TESTPOOL
done
async_mirror_stop_sync_writer
sync_pool $TESTPOOL
log_must test -s /$TESTPOOL/w
log_must wait_async_mirror_idle

typeset guid=$(get_leaf_guid $ASYNC_MIRROR_LAGGING)
log_must zinject -d $guid -D 100:1 $TESTPOOL
for (( i = 0; i < 20; i++ )); do
	async_mirror_write $FS2/churn.$i 16
	sync_pool $TESTPOOL
done
async_mirror_suspend_copy 1
log_must zinject -c all
log_must zpool freeze $TESTPOOL
async_mirror_start_sync_writer /$TESTPOOL/w2
sleep 2
async_mirror_stop_sync_writer
log_must zpool export $TESTPOOL
async_mirror_note_zil

async_mirror_import_lagging
log_must test "$digests" == "$(async_mirror_digests a 1)"
[[ -e /$TESTPOOL/w ]] && log_must async_mirror_only_x /$TESTPOOL/w
[[ -e /$TESTPOOL/w2 ]] && log_must async_mirror_only_x /$TESTPOOL/w2
async_mirror_check_clean $ASYNC_MIRROR_LAGGING
log_must zpool export $TESTPOOL
async_mirror_unhide_leader

log_pass "A log block that predates the switch does not hurt the child."
