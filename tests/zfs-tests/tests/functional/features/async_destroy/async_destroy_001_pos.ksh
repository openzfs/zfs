#!/bin/ksh -p
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or https://opensource.org/licenses/CDDL-1.0.
# See the License for the specific language governing permissions
# and limitations under the License.
#
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
# CDDL HEADER END
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
