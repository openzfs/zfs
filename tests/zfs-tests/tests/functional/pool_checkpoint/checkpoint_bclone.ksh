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

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#	Blocks that a checkpoint references must not be reused while the
#	checkpoint exists, even when they are freed through a block clone
#	(openzfs/zfs#19225).
#
# STRATEGY:
#	1. Create a small pool with block cloning and write a file.
#	2. Take a checkpoint.
#	3. Clone the file, then remove the original and, in a later txg, the
#	   clone.  The clone's block pointer is born after the checkpoint,
#	   but its blocks were allocated before it.
#	4. Initialize the pool, which writes a pattern to all of its free
#	   space, so any space that was wrongly freed is overwritten.
#	5. Rewind to the checkpoint.  The file must read back intact and a
#	   scrub must find no errors.
#

verify_runnable "global"

typeset pool=cp_bclone
typeset vdev=$TEST_BASE_DIR/cp_bclone.vdev

function cleanup
{
	poolexists $pool && destroy_pool $pool
	rm -f $vdev
	log_must restore_tunable BCLONE_ENABLED
}

log_onexit cleanup

log_assert "Blocks freed through a clone stay in the checkpoint"

log_must save_tunable BCLONE_ENABLED
log_must set_tunable32 BCLONE_ENABLED 1

log_must truncate -s 256M $vdev
log_must zpool create -o feature@block_cloning=enabled \
    -O recordsize=128k -O compression=off $pool $vdev
typeset fs=$(get_prop mountpoint $pool)

log_must dd if=/dev/urandom of=$fs/orig bs=1M count=16
log_must sync_pool $pool
typeset sum=$(xxh128digest $fs/orig)

log_must zpool checkpoint $pool

log_must clonefile -f $fs/orig $fs/clone 0 0 $((16 * 1024 * 1024))
log_must sync_pool $pool
typeset used=$(get_pool_prop bcloneused $pool)
[[ "$used" != "0" ]] || log_fail "the file was copied, not cloned"
log_must rm $fs/orig
log_must sync_pool $pool
log_must rm $fs/clone
# Freed space is deferred for a few txgs before it can be reused.
for i in 1 2 3 4; do
	log_must sync_pool $pool
done

#
# Overwrite all free space.  (Filling the pool with files does not do it
# reliably: the allocator need not use the freed range before the pool
# runs into its slop space, which is half of a pool this small.)
#
log_must zpool initialize -w $pool

log_must zpool export $pool
log_must zpool import -d $TEST_BASE_DIR --rewind-to-checkpoint $pool

[[ "$(xxh128digest $fs/orig)" == "$sum" ]] || \
    log_fail "file from the checkpoint does not read back intact"
log_must zpool scrub -w $pool
log_must check_pool_status $pool "errors" "No known data errors"

log_pass "Blocks freed through a clone stay in the checkpoint"
