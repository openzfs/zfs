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
# zdb -l will report corrupted labels checksums
#
# Strategy:
# 1. Create pool with some number of vdevs and export it
# 2. Corrupt label 0 and label 1, check that corrupted labels are reported
# 3. Check that pool still be imported correctly
# 4. Corrupt all labels, check that all corrupted labels are reported
# 5. Check that pool cannot be imported
#

log_assert "Verify zdb -l will report corrupted labels checksums"
log_onexit cleanup

VIRTUAL_DISK=$TEST_BASE_DIR/disk

function cleanup
{
	poolexists $TESTPOOL && log_must destroy_pool $TESTPOOL
	[[ -f $VIRTUAL_DISK ]] && log_must rm $VIRTUAL_DISK
}

verify_runnable "global"

log_must truncate -s $(($MINVDEVSIZE * 8)) $VIRTUAL_DISK

log_must zpool create $TESTPOOL $VIRTUAL_DISK
log_must zpool export $TESTPOOL

corrupt_label_checksum 0 $VIRTUAL_DISK
corrupt_label_checksum 1 $VIRTUAL_DISK

msg_count=$(zdb -l $VIRTUAL_DISK | grep -c '(Bad label cksum)')
[ $msg_count -ne 1 ] && \
    log_fail "zdb -l produces an incorrect number of corrupted labels."

msg_count=$(zdb -lll $VIRTUAL_DISK | grep -c '(Bad label cksum)')
[ $msg_count -ne 2 ] && \
    log_fail "zdb -l produces an incorrect number of corrupted labels."

log_must zpool import $TESTPOOL -d $TEST_BASE_DIR
log_must zpool export $TESTPOOL

corrupt_label_checksum 0 $VIRTUAL_DISK
corrupt_label_checksum 1 $VIRTUAL_DISK
corrupt_label_checksum 2 $VIRTUAL_DISK
corrupt_label_checksum 3 $VIRTUAL_DISK

msg_count=$(zdb -lll $VIRTUAL_DISK | grep -c '(Bad label cksum)')
[ $msg_count -ne 4 ] && \
    log_fail "zdb -l produces an incorrect number of corrupted labels."

log_mustnot zpool import $TESTPOOL -d $TEST_BASE_DIR

cleanup

log_pass "zdb -l bad cksum report is correct."
