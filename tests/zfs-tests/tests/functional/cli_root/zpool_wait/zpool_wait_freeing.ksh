#!/bin/ksh -p
#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# http://www.illumos.org/license/CDDL.
#

#
# Copyright (c) 2018 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_wait/zpool_wait.kshlib

#
# DESCRIPTION:
# 'zpool wait' works when waiting for background freeing to complete.
#
# STRATEGY:
# 1. Create a pool.
# 2. Modify tunables to make sure freeing is slow enough to observe.
# 3. Create a file system with some data.
# 4. Destroy the file system and call 'zpool wait'.
# 5. Monitor the waiting process to make sure it returns neither too soon nor
#    too late.
# 6. Repeat 3-5, except destroy a snapshot instead of a filesystem.
# 7. Repeat 3-5, except destroy a clone.
#

function cleanup
{
	log_must set_tunable64 ASYNC_BLOCK_MAX_BLOCKS $default_async_block_max_blocks
	log_must set_tunable64 LIVELIST_MAX_ENTRIES $default_max_livelist_entries
	log_must set_tunable64 LIVELIST_MIN_PERCENT_SHARED $default_min_pct_shared

	poolexists $TESTPOOL && destroy_pool $TESTPOOL
	kill_if_running $pid
}

function test_wait
{
	log_bkgrnd zpool wait -t free $TESTPOOL
	pid=$!
	check_while_waiting $pid '[[ $(get_pool_prop freeing $TESTPOOL) != "0" ]]'
}

typeset -r FS="$TESTPOOL/$TESTFS1"
typeset -r SNAP="$FS@snap1"
typeset -r CLONE="$TESTPOOL/clone"
typeset pid default_max_livelist_entries default_min_pct_shared
typeset default_async_block_max_blocks

log_onexit cleanup

log_must zpool create $TESTPOOL $DISK1

#
# Limit the number of blocks that can be freed in a single txg. This slows down
# freeing so that we actually have something to wait for.
#
default_async_block_max_blocks=$(get_tunable ASYNC_BLOCK_MAX_BLOCKS)
log_must set_tunable64 ASYNC_BLOCK_MAX_BLOCKS 8
#
# Space from clones gets freed one livelist per txg instead of being controlled
# by zfs_async_block_max_blocks. Limit the rate at which space is freed by
# limiting the size of livelists so that we end up with a number of them.
#
default_max_livelist_entries=$(get_tunable LIVELIST_MAX_ENTRIES)
log_must set_tunable64 LIVELIST_MAX_ENTRIES 16
# Don't disable livelists, no matter how much clone diverges from snapshot
default_min_pct_shared=$(get_tunable LIVELIST_MIN_PERCENT_SHARED)
log_must set_tunable64 LIVELIST_MIN_PERCENT_SHARED -1

#
# Test waiting for space from destroyed filesystem to be freed
#
log_must zfs create "$FS"
log_must dd if=/dev/zero of="/$FS/testfile" bs=1M count=128
log_must zfs destroy "$FS"
test_wait

#
# Test waiting for space from destroyed snapshot to be freed
#
log_must zfs create "$FS"
log_must dd if=/dev/zero of="/$FS/testfile" bs=1M count=128
log_must zfs snapshot "$SNAP"
# Make sure bulk of space is unique to snapshot
log_must rm "/$FS/testfile"
log_must zfs destroy "$SNAP"
test_wait

#
# Test waiting for space from destroyed clone to be freed
#
log_must zfs snapshot "$SNAP"
log_must zfs clone "$SNAP" "$CLONE"
# Add some data to the clone
for i in {1..50}; do
	log_must dd if=/dev/urandom of="/$CLONE/testfile$i" bs=1k count=512
	# Force each new file to be tracked by a new livelist
	sync_pool $TESTPOOL
done
log_must zfs destroy "$CLONE"
test_wait

log_pass "'zpool wait -t freeing' works."
