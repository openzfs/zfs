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
# Copyright (c) 2016 by Lawrence Livermore National Security, LLC.
# Use is subject to license terms.
#

. $STF_SUITE/include/libtest.shlib

verify_runnable "both"

TEST_SEND_FS=$TESTPOOL/send_large_dnode
TEST_RECV_FS=$TESTPOOL/recv_large_dnode
TEST_SNAP=$TEST_SEND_FS@ldnsnap
TEST_SNAPINCR=$TEST_SEND_FS@ldnsnap_incr
TEST_STREAM=$TESTDIR/ldnsnap
TEST_STREAMINCR=$TESTDIR/ldnsnap_incr
TEST_FILE=foo
TEST_FILEINCR=bar

function cleanup
{
	datasetexists $TEST_SEND_FS && destroy_dataset $TEST_SEND_FS -r
	datasetexists $TEST_RECV_FS && destroy_dataset $TEST_RECV_FS -r

	rm -f $TEST_STREAM
	rm -f $TEST_STREAMINCR
}

log_onexit cleanup

log_assert "zfs send stream with large dnodes accepted by new pool"

log_must zfs create -o dnodesize=1k $TEST_SEND_FS
log_must touch /$TEST_SEND_FS/$TEST_FILE
log_must zfs snap $TEST_SNAP
log_must eval "zfs send $TEST_SNAP > $TEST_STREAM"
log_must rm -f /$TEST_SEND_FS/$TEST_FILE
log_must touch /$TEST_SEND_FS/$TEST_FILEINCR
log_must zfs snap $TEST_SNAPINCR
log_must eval "zfs send -i $TEST_SNAP $TEST_SNAPINCR > $TEST_STREAMINCR"

log_must eval "zfs recv $TEST_RECV_FS < $TEST_STREAM"
inode=$(ls -li /$TEST_RECV_FS/$TEST_FILE | awk '{print $1}')
dnsize=$(zdb -dddd $TEST_RECV_FS $inode | awk '/ZFS plain file/ {print $6}')
if [[ "$dnsize" != "1K" ]]; then
	log_fail "dnode size is $dnsize (expected 1K)"
fi

log_must eval "zfs recv -F $TEST_RECV_FS < $TEST_STREAMINCR"
log_must directory_diff /$TEST_SEND_FS /$TEST_RECV_FS
log_must zfs umount $TEST_SEND_FS
log_must zfs umount $TEST_RECV_FS

log_pass
