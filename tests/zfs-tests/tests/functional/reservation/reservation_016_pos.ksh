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
# In pool with a full filesystem and a regular volume (with implicit
# reservation) destroying the volume should allow more data to be written
# to the filesystem
#
#
# STRATEGY:
# 1) Create a regular (non-sparse) volume
# 2) Create a filesystem at the same level
# 3) Fill up the filesystem
# 4) Destroy the volume
# 5) Verify can write more data to the filesystem
#

verify_runnable "global"

log_assert "Destroying a regular volume with reservation allows more data to" \
    " be written to top level filesystem"

function cleanup
{
	datasetexists $TESTPOOL/$TESTVOL && \
	    destroy_dataset $TESTPOOL/$TESTVOL

	[[ -e $TESTDIR/$TESTFILE1 ]] && log_must rm -rf $TESTDIR/$TESTFILE1
	[[ -e $TESTDIR/$TESTFILE2 ]] && log_must rm -rf $TESTDIR/$TESTFILE2
}
log_onexit cleanup

space_avail=$(largest_volsize_from_pool $TESTPOOL)

#
# To make sure this test doesn't take too long to execute on
# large pools, we calculate a volume size which will ensure we
# have RESV_FREE_SPACE left free in the pool.
#
((vol_set_size = space_avail - RESV_FREE_SPACE))
vol_set_size=$(floor_volsize $vol_set_size)

# Creating a regular volume implicitly sets its reservation
# property to the same value.
log_must zfs create -V $vol_set_size $TESTPOOL/$TESTVOL
block_device_wait $TESTPOOL/$TESTVOL

space_avail_still=$(get_prop available $TESTPOOL)
fill_size=$((space_avail_still + $RESV_TOLERANCE))
write_count=$((fill_size / BLOCK_SIZE))

# Now fill up the filesystem (which doesn't have a reservation set
# and thus will use up whatever free space is left in the pool).
file_write -o create -f $TESTDIR/$TESTFILE1 -b $BLOCK_SIZE -c $write_count -d 0
ret=$?
if (($ret != $ENOSPC)); then
	log_fail "Did not get ENOSPC as expected (got $ret)."
fi

log_must zfs destroy -f $TESTPOOL/$TESTVOL

log_must file_write -o create -f $TESTDIR/$TESTFILE2 \
    -b $(getconf PAGESIZE) -c 1000 -d 0

log_pass "Destroying volume with reservation allows more data to be written " \
    "to top level filesystem"
