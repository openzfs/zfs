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
#	The blocks of a claimed intent log on the mirror are not reused
#	while the lagging child is between the claim and the replay.  The
#	replay copies them to the lagging child in the claim txg, from the
#	leader, with no checksum, so a block reused before that copy reaches
#	the lagging child with the new bytes.  See
#	async_mirror_claimed_slog_pos for
#	a log device.
#
# STRATEGY:
#	1. Create a pool with a two-way mirror and a second file system,
#	   fs2.  Turn the async mirror on.  Create the intent log with one
#	   synchronous write, and wait until the lagging child has caught up.
#	2. Freeze the pool.  Write a file with synchronous writes: only the
#	   intent log has it.  Export.
#	3. Stop the copy, and import without mounting: the import claims the
#	   log.  Mount fs2, write file f in it, and damage its indirect block
#	   on the leader, so that the copy of its txg stops there.
#	4. Mount the pool's file system: the replay frees the claimed log.
#	   Write much data to fs2, so that the allocator reuses free blocks.
#	5. Restart the copy, reading the leader, and wait until the lagging
#	   child freezes before the txg of file f, after the claim.  Export.
#	6. Import the lagging child alone.  Verify the file of step 2.
#

verify_runnable "global"

function cleanup
{
	[[ -e $ASYNC_MIRROR_HIDDEN ]] && async_mirror_unhide_leader
	cleanup_async_mirror
	rm -rf $FS2
}

log_onexit cleanup

# Outside the pool's file system, which is mounted after fs2.
FS2=$TEST_BASE_DIR/async_mirror_fs2
# The churn writes about half of the pool.
ASYNC_MIRROR_VDEV_SIZE=512m

async_mirror_create_pool
log_must zfs create -o mountpoint=$FS2 $TESTPOOL/fs2
async_mirror_enable
log_must dd if=/dev/zero of=/$TESTPOOL/sync conv=fdatasync,fsync bs=1 count=1
log_must wait_async_mirror_idle

log_must zpool freeze $TESTPOOL
log_must dd if=/dev/urandom of=/$TESTPOOL/payload oflag=sync bs=1k count=8
typeset digest=$(xxh128digest /$TESTPOOL/payload)
log_must zpool export $TESTPOOL

log_must async_mirror_set_tunable32 ASYNC_MIRROR_READ_RETRIES 1
async_mirror_suspend_copy 1
log_must zpool import -N -d $ASYNC_MIRROR_DIR $TESTPOOL
log_must test "$(get_async_mirror_stat active)" == 1
log_must zfs mount $TESTPOOL/fs2
async_mirror_write $FS2/f 16
sync_pool $TESTPOOL
async_mirror_corrupt_leader $FS2/f 1

log_must zfs mount $TESTPOOL
log_must test "$digest" == "$(xxh128digest /$TESTPOOL/payload)"
typeset -i i
for (( i = 0; i < 40; i++ )); do
	async_mirror_write $FS2/churn.$i 48
	sync_pool $TESTPOOL
done

#
# Drop the blocks of f from memory, so that the copy reads the leader.
# An unmount releases the dbufs that hold them; only then can the ARC
# flush evict them.
#
log_must zfs unmount $TESTPOOL/fs2
log_must zinject -a
async_mirror_suspend_copy 0
log_must wait_async_mirror_frozen
log_note "the lagging child is frozen at txg" \
    "$(get_async_mirror_stat frozen_txg)"
log_must zpool export $TESTPOOL

async_mirror_import_lagging
log_must test -f /$TESTPOOL/payload
log_must test "$digest" == "$(xxh128digest /$TESTPOOL/payload)"
log_must zpool export $TESTPOOL
async_mirror_unhide_leader

log_pass "A claimed log on the mirror is kept for the lagging child."
