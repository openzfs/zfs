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
# http://www.illumos.org/license/CDDL.
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
verify_runnable "both"

function cleanup
{
	destroy_pool testpool_recv_blksz
	rm -f $TEST_BASE_DIR/testpool_recv_blksz $TEST_BASE_DIR/send
}

log_assert "Verify that send recv works fine with different blksz."
log_onexit cleanup

log_must eval "gzip -cd $TESTPOOL_FILE | dd of=$TEST_BASE_DIR/testpool_recv_blksz conv=sparse"
log_must zpool import -d $TEST_BASE_DIR testpool_recv_blksz
log_must eval "zfs send -i @snap2 $TESTFS1@snap3 >$TEST_BASE_DIR/send"
log_must eval "zfs recv $TESTFS2 <$TEST_BASE_DIR/send"

if ! cmp /$TESTFS1/test1 /$TESTFS2/test1; then
	log_fail "File test1 mismatch after recv."
fi

if ! cmp /$TESTFS1/test2 /$TESTFS2/test2; then
	log_fail "File test2 mismatch after recv."
fi

if ! cmp /$TESTFS1/test3 /$TESTFS2/test3; then
	log_fail "File test3 mismatch after recv."
fi

log_pass "Send recv works fine fine with different blksz."
