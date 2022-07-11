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
