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
# Copyright (c) 2026, iXsystems, Inc.
#

# DESCRIPTION:
#	Verify zfs rewrite -C flag skips BRT-cloned blocks.
#
# STRATEGY:
#	1. Create a test file and sync it.
#	2. Clone the file using block cloning to share blocks via BRT.
#	3. Rewrite clone with -C flag and verify blocks are NOT rewritten.
#	4. Rewrite clone without -C flag and verify blocks ARE rewritten.

. $STF_SUITE/include/libtest.shlib

verify_runnable "global"

function cleanup
{
	rm -rf $TESTDIR/*
}

log_assert "zfs rewrite -C flag skips BRT-cloned blocks"

log_onexit cleanup

log_must zfs set recordsize=128k $TESTPOOL/$TESTFS

# Create source file (4 x 128KB = 4 blocks)
log_must dd if=/dev/urandom of=$TESTDIR/source bs=128k count=4
log_must sync_pool $TESTPOOL

# Clone the file using block cloning
log_must clonefile -f $TESTDIR/source $TESTDIR/clone
log_must sync_pool $TESTPOOL

# Verify blocks are actually shared initially
typeset blocks=$(get_same_blocks $TESTPOOL/$TESTFS source \
    $TESTPOOL/$TESTFS clone)
log_must [ "$blocks" = "0 1 2 3" ]

# Test 1: Rewrite clone WITH -C flag (should skip all cloned blocks)
log_must zfs rewrite -C $TESTDIR/clone
log_must sync_pool $TESTPOOL

# Blocks should still be shared (all blocks were skipped)
typeset blocks=$(get_same_blocks $TESTPOOL/$TESTFS source \
    $TESTPOOL/$TESTFS clone)
log_must [ "$blocks" = "0 1 2 3" ]

# Test 2: Rewrite clone WITHOUT -C flag (should rewrite all blocks)
log_must zfs rewrite $TESTDIR/clone
log_must sync_pool $TESTPOOL

# No blocks should be shared (clone has new blocks)
typeset blocks=$(get_same_blocks $TESTPOOL/$TESTFS source \
    $TESTPOOL/$TESTFS clone)
log_must [ -z "$blocks" ]

log_pass
