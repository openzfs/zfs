#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
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
# Copyright (c) 2026, iXsystems, Inc.
#

# DESCRIPTION:
#	Verify zfs rewrite -S flag skips snapshot-shared blocks.
#
# STRATEGY:
#	1. Create a test file and sync it.
#	2. Take a snapshot to share the blocks.
#	3. Rewrite with -S flag and verify blocks are NOT rewritten.
#	4. Rewrite without -S flag and verify blocks ARE rewritten.

. $STF_SUITE/include/libtest.shlib

function cleanup
{
	rm -rf $TESTDIR/*
	zfs destroy -R $TESTPOOL/$TESTFS@snap1 2>/dev/null || true
}

log_assert "zfs rewrite -S flag skips snapshot-shared blocks"

log_onexit cleanup

log_must zfs set recordsize=128k $TESTPOOL/$TESTFS

# Create test file (4 x 128KB = 4 blocks) and snapshot
log_must dd if=/dev/urandom of=$TESTDIR/testfile bs=128k count=4
log_must sync_pool $TESTPOOL
log_must zfs snapshot $TESTPOOL/$TESTFS@snap1

# Test 1: Rewrite WITH -S flag (should skip all snapshot-shared blocks)
log_must zfs rewrite -S $TESTDIR/testfile
log_must sync_pool $TESTPOOL

# All blocks should still be shared (all blocks were skipped)
typeset blocks=$(get_same_blocks $TESTPOOL/$TESTFS testfile \
    $TESTPOOL/$TESTFS@snap1 testfile)
log_must [ "$blocks" = "0 1 2 3" ]

# Test 2: Rewrite WITHOUT -S flag (should rewrite all blocks)
log_must zfs rewrite $TESTDIR/testfile
log_must sync_pool $TESTPOOL

# No blocks should be shared (all blocks were rewritten)
typeset blocks=$(get_same_blocks $TESTPOOL/$TESTFS testfile \
    $TESTPOOL/$TESTFS@snap1 testfile)
log_must [ -z "$blocks" ]

log_pass
