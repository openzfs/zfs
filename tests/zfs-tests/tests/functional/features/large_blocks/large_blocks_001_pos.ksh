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
# Copyright (c) 2026 Tigerblue77
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#	large_blocks becomes active once a dataset contains a file or a
#	volume whose block size is larger than 128K, before any block that
#	large is written.
#
# STRATEGY:
#	1. Create a pool on which large_blocks is enabled.
#	2. In a file system with recordsize=1M, grow an empty file with
#	   truncate: its block size becomes 1M and it has no block.
#	   large_blocks must be active, and enabled again once the file
#	   system is destroyed.
#	3. Create a volume with volblocksize=1M and write nothing:
#	   large_blocks must be active, and enabled again once the volume
#	   is destroyed.
#

verify_runnable "global"

VDEV=$TEST_BASE_DIR/large_blocks_001.vdev

function cleanup
{
	poolexists $TESTPOOL1 && destroy_pool $TESTPOOL1
	rm -f $VDEV
}

function check_feature # value
{
	typeset val=$(get_pool_prop feature@large_blocks $TESTPOOL1)

	[[ "$val" == "$1" ]] ||
	    log_fail "feature@large_blocks is '$val', expected '$1'"
}

log_assert "A file or a volume with a block size larger than 128K" \
    "activates large_blocks before such a block is written"
log_onexit cleanup

# 1.
log_must truncate -s $MINVDEVSIZE $VDEV
log_must zpool create -f $TESTPOOL1 $VDEV
check_feature enabled

# 2. A file with a 1M block size and no block.
log_must zfs create -o recordsize=1M $TESTPOOL1/fs
mntpnt=$(get_prop mountpoint $TESTPOOL1/fs)
log_must truncate -s 10M $mntpnt/file
sync_pool $TESTPOOL1
check_feature active
log_must zfs destroy $TESTPOOL1/fs
check_feature enabled

# 3. A volume with 1M blocks and nothing written.
log_must zfs create -V 16M -b 1M $TESTPOOL1/vol
block_device_wait
sync_pool $TESTPOOL1
check_feature active
destroy_dataset $TESTPOOL1/vol
check_feature enabled

log_pass "A file or a volume with a block size larger than 128K" \
    "activates large_blocks before such a block is written"
