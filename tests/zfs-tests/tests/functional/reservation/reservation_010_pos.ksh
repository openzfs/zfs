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
