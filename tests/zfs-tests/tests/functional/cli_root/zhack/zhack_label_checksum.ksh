#!/bin/ksh

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
# Copyright (c) 2021 by vStack. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/include/blkdev.shlib

#
# Description:
# zhack label repair <vdev> will calculate and rewrite label checksum if invalid
#
# Strategy:
# 1. Create pool with some number of vdevs and export it
# 2. Corrupt all labels checksums
# 3. Check that pool cannot be imported
# 4. Use zhack to repair labels checksums
# 5. Check that pool can be imported
#

log_assert "Verify zhack label repair <vdev> will repair labels checksums"
log_onexit cleanup

VIRTUAL_DISK=$TEST_BASE_DIR/disk

function cleanup
{
	poolexists $TESTPOOL && destroy_pool $TESTPOOL
	[[ -f $VIRTUAL_DISK ]] && log_must rm $VIRTUAL_DISK
}

log_must truncate -s $(($MINVDEVSIZE * 8)) $VIRTUAL_DISK

log_must zpool create $TESTPOOL $VIRTUAL_DISK
log_must zpool export $TESTPOOL

log_mustnot zhack label repair $VIRTUAL_DISK

corrupt_label_checksum 0 $VIRTUAL_DISK
corrupt_label_checksum 1 $VIRTUAL_DISK
corrupt_label_checksum 2 $VIRTUAL_DISK
corrupt_label_checksum 3 $VIRTUAL_DISK

log_mustnot zpool import $TESTPOOL -d $TEST_BASE_DIR

log_must zhack label repair $VIRTUAL_DISK

log_must zpool import $TESTPOOL -d $TEST_BASE_DIR

cleanup

log_pass "zhack label repair works correctly."
