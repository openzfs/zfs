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

. $STF_SUITE/include/libtest.shlib

verify_runnable "both"

TEST_SEND_FS=$TESTPOOL/send_large_dnode
TEST_RECV_FS=$TESTPOOL/recv_large_dnode
TEST_SNAP=$TEST_SEND_FS@ldnsnap
TEST_STREAM=$TESTDIR/ldnsnap
TEST_FILE=foo


function cleanup
{
	if datasetexists $TEST_SEND_FS ; then
		log_must $ZFS destroy -r $TEST_SEND_FS
	fi

	if datasetexists $TEST_RECV_FS ; then
		log_must $ZFS destroy -r $TEST_RECV_FS
	fi

	rm -f $TEST_STREAM
}

log_onexit cleanup

log_assert "zfs send stream with large dnodes accepted by new pool"

log_must $ZFS create -o dnodesize=1k $TEST_SEND_FS
log_must touch /$TEST_SEND_FS/$TEST_FILE
log_must $ZFS umount $TEST_SEND_FS
log_must $ZFS snap $TEST_SNAP
log_must $ZFS send $TEST_SNAP > $TEST_STREAM

log_must eval "$ZFS recv $TEST_RECV_FS < $TEST_STREAM"
inode=$(ls -li /$TEST_RECV_FS/$TEST_FILE | awk '{print $1}')
dnsize=$($ZDB -dddd $TEST_RECV_FS $inode | awk '/ZFS plain file/ {print $6}')
if [[ "$dnsize" != "1K" ]]; then
	log_fail "dnode size is $dnsize (expected 1K)"
fi

log_pass
