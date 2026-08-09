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
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/reservation/reservation.shlib

#
# DESCRIPTION:
#
# In pool with a full filesystem and a filesystem with a reservation
# destroying another filesystem should allow more data to be written to
# the full filesystem
#
#
# STRATEGY:
# 1) Create a filesystem as dataset
# 2) Create a filesystem at the same level
# 3) Set a reservation on the dataset filesystem
# 4) Fill up the second filesystem
# 5) Destroy the dataset filesystem
# 6) Verify can write more data to the full filesystem
#

verify_runnable "both"

log_assert "Destroying top level filesystem with reservation allows more " \
    "data to be written to another top level filesystem"

function cleanup
{
	datasetexists $TESTPOOL/$TESTFS1 && \
		destroy_dataset $TESTPOOL/$TESTFS1

	[[ -e $TESTDIR/$TESTFILE1 ]] && log_must rm -rf $TESTDIR/$TESTFILE1
	[[ -e $TESTDIR/$TESTFILE2 ]] && log_must rm -rf $TESTDIR/$TESTFILE2
}

log_onexit cleanup

log_must zfs create $TESTPOOL/$TESTFS1

space_avail=`get_prop available $TESTPOOL`

#
# To make sure this test doesn't take too long to execute on
# large pools, we calculate a reservation setting which when
# applied to the dataset filesystem  will ensure we have
# RESV_FREE_SPACE left free in the pool.
#
((resv_size_set = space_avail - RESV_FREE_SPACE))

log_must zfs set reservation=$resv_size_set $TESTPOOL/$TESTFS1

space_avail_still=`get_prop available $TESTPOOL`

fill_size=`expr $space_avail_still + $RESV_TOLERANCE`
write_count=`expr $fill_size / $BLOCK_SIZE`

# Now fill up the filesystem (which doesn't have a reservation set
# and thus will use up whatever free space is left in the pool).
file_write -o create -f $TESTDIR/$TESTFILE1 -b $BLOCK_SIZE -c $write_count -d 0
ret=$?
if (($ret != $ENOSPC)); then
	log_fail "Did not get ENOSPC as expected (got $ret)."
fi

log_must zfs destroy -f $TESTPOOL/$TESTFS1

log_must file_write -o create -f $TESTDIR/$TESTFILE2 \
    -b $(getconf PAGESIZE) -c 1000 -d 0

log_pass "Destroying top level filesystem with reservation allows more data " \
    "to be written to another top level filesystem"
