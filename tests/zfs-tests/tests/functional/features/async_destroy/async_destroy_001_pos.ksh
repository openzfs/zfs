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
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Exercise the traversal suspend/resume code in async_destroy by
# destroying a file system that has more blocks than we can free
# in a single txg.
#
# STRATEGY:
# 1. Create a file system
# 2. Set recordsize to 512 to create the maximum number of blocks
# 3. Set compression to off to force zero-ed blocks to be written
# 4. dd a lot of data from /dev/zero to the file system
# 5. Destroy the file system
# 6. Wait for the freeing property to go to 0
# 7. Use zdb to check for leaked blocks
#

TEST_FS=$TESTPOOL/async_destroy

verify_runnable "both"

function cleanup
{
	datasetexists $TEST_FS && destroy_dataset $TEST_FS
	log_must set_tunable64 ASYNC_BLOCK_MAX_BLOCKS 100000
}

log_onexit cleanup
log_assert "async_destroy can suspend and resume traversal"

log_must zfs create -o recordsize=1k -o compression=off $TEST_FS

# Fill with 128,000 blocks.
log_must dd bs=1024k count=128 if=/dev/zero of=/$TEST_FS/file

#
# Decrease the max blocks to free each txg, so that freeing takes
# long enough that we can observe it.
#
log_must set_tunable64 ASYNC_BLOCK_MAX_BLOCKS 100

sync_all_pools
log_must zfs destroy $TEST_FS

#
# We monitor the freeing property, to verify we can see blocks being
# freed while the suspend/resume code is exercised.
#
t0=$SECONDS
count=0
while [[ $((SECONDS - t0)) -lt 10 ]]; do
	[[ "0" != "$(zpool list -Ho freeing $TESTPOOL)" ]] && ((count++))
	[[ $count -gt 1 ]] && break
	sleep 1
done

[[ $count -eq 0 ]] && log_fail "Freeing property remained empty"

#
# After a bit, go back to allowing an unlimited amount of freeing
# per txg.
#
sleep 10
log_must set_tunable64 ASYNC_BLOCK_MAX_BLOCKS 100000

# Wait for everything to be freed.
while [[ "0" != "$(zpool list -Ho freeing $TESTPOOL)" ]]; do
	[[ $((SECONDS - t0)) -gt 180 ]] && \
	    log_fail "Timed out waiting for freeing to drop to zero"
done

# Check for leaked blocks.
log_must zdb -b $TESTPOOL

log_pass "async_destroy can suspend and resume traversal"
