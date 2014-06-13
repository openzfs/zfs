#!/bin/ksh -p
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
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
# Copyright (c) 2013 by Delphix. All rights reserved.
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
	datasetexists $TEST_FS && log_must $ZFS destroy $TEST_FS
}

log_onexit cleanup
log_assert "async_destroy can suspend and resume traversal"

log_must $ZFS create -o recordsize=512 -o compression=off $TEST_FS

#
# Fill with 2G
#
log_must $DD bs=1024k count=2048 if=/dev/zero of=/$TEST_FS/file

log_must $ZFS destroy $TEST_FS

count=0
while [[ "0" != "$($ZPOOL list -Ho freeing $TESTPOOL)" ]]; do
	count=$((count + 1))
	sleep 1
done

#
# We assert that the data took a few seconds to free to make sure that
# we actually exercised the suspend/resume code. The destroy should
# actually take much longer than this, so false positives are not likely.
#
log_must test $count -gt 5

#
# Check for leaked blocks.
#
log_must $ZDB -b $TESTPOOL

log_pass "async_destroy can suspend and resume traversal"
