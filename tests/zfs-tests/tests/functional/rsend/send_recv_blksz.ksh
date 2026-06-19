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
# Copyright (c) 2026 by Nutanix. All rights reserved.
#

. $STF_SUITE/tests/functional/rsend/rsend.kshlib
. $STF_SUITE/include/properties.shlib

#
# Description:
# Verify that send recv works fine with different blksz.
#
# Strategy:
# 1. Use precreated pool image with 2 fs with files with different blksz.
# 2. Verify that the send and recv works fine.
# 3. Verify file content is the same after recv.
#

typeset TESTPOOL_FILE=$STF_SUITE/tests/functional/rsend/testpool_recv_blksz.gz
typeset TESTFS1=testpool_recv_blksz/testfs1
typeset TESTFS2=testpool_recv_blksz/testfs2
verify_runnable "global"

function cleanup
{
	restore_tunable CHECK_FREE_RANGE_N
	destroy_pool testpool_recv_blksz
	rm -f $TEST_BASE_DIR/{testpool_recv_blksz,send,send2,dump}
}

log_assert "Verify that send recv works fine with different blksz."
log_onexit cleanup

log_must save_tunable CHECK_FREE_RANGE_N
log_must set_tunable32 CHECK_FREE_RANGE_N 16

log_must eval "gzip -cd $TESTPOOL_FILE | dd of=$TEST_BASE_DIR/testpool_recv_blksz conv=sparse"
log_must zpool import -d $TEST_BASE_DIR testpool_recv_blksz
log_must eval "zfs send -i @snap2 $TESTFS1@snap3 >$TEST_BASE_DIR/send"
log_must eval "zfs recv $TESTFS2 <$TEST_BASE_DIR/send"

for i in $(seq 4); do
	if ! cmp /$TESTFS1/test$i /$TESTFS2/test$i; then
		log_fail "File test$i mismatch after recv."
	fi
done

# Test WRITE_EMBEDDED for different blksz
log_must zfs set compress=gzip-9 $TESTFS1
log_must dd if=/dev/zero of=/$TESTFS1/test3 bs=64k count=1 conv=notrunc
log_must eval "echo 'z' | dd of=/$TESTFS1/test3 bs=1 count=1 conv=notrunc"

log_must zfs snap $TESTFS1@snap4
log_must eval "zfs send -e -i @snap3 $TESTFS1@snap4 >$TEST_BASE_DIR/send2"
log_must eval "zstream dump -v <$TEST_BASE_DIR/send2 >$TEST_BASE_DIR/dump"
grep -q "WRITE_EMBEDDED object" $TEST_BASE_DIR/dump || \
    log_fail "Embedded record not found in stream"
log_must eval "zfs recv $TESTFS2 <$TEST_BASE_DIR/send2"


if ! cmp /$TESTFS1/test3 /$TESTFS2/test3; then
	log_fail "File test3 mismatch after recv."
fi

log_pass "Send recv works fine fine with different blksz."
