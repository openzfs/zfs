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
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
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
# In pool with a full filesystem and another filesystem with a reservation
# setting the reservation on the second filesystem to 'none' should allow more
# data to be written to the first filesystem.
#
#
# STRATEGY:
# 1) Create a filesystem as a dataset
# 2) Create a filesystem at the same level
# 3) Set a reservation on the dataset filesystem
# 4) Fill up the filesystem
# 5) Set the reservation on the dataset filesystem to 'none'
# 6) Verify we can write more data to the first filesystem
#

verify_runnable "both"

log_assert "Setting top level dataset reservation to 'none' allows more data " \
    "to be written to top level filesystem"

function cleanup
{
	log_must rm -rf $TESTDIR/$TESTFILE1
	log_must rm -rf $TESTDIR/$TESTFILE2

	datasetexists $TESTPOOL/$TESTFS1 && \
		destroy_dataset $TESTPOOL/$TESTFS1 -f
}

log_onexit cleanup

log_must zfs create $TESTPOOL/$TESTFS1

space_avail=`get_prop available $TESTPOOL`

#
# To make sure this test doesn't take too long to execute on
# large pools, we calculate a reservation setting which when
# applied to the dataset will ensure we have RESV_FREE_SPACE
# left free in the pool which we can quickly fill.
#
((resv_size_set = space_avail - RESV_FREE_SPACE))

log_must zfs set reservation=$resv_size_set $TESTPOOL/$TESTFS1

space_avail_still=`get_prop available $TESTPOOL`

fill_size=`expr $space_avail_still + $RESV_TOLERANCE`
write_count=`expr $fill_size / $BLOCK_SIZE`

# Now fill up the filesystem (which doesn't have a reservation set
# and thus will use up whatever free space is left in the pool).
file_write -o create -f $TESTDIR/$TESTFILE1 -b $BLOCK_SIZE \
        -c $write_count -d 0
ret=$?
if (($ret != $ENOSPC)); then
	log_fail "Did not get ENOSPC as expected (got $ret)."
fi

log_must zfs set reservation=none $TESTPOOL/$TESTFS1

log_must file_write -o create -f $TESTDIR/$TESTFILE2 \
    -b $(getconf PAGESIZE) -c 1000 -d 0

log_pass "Setting top level dataset reservation to 'none' allows more " \
    "data to be written to the top level filesystem"
