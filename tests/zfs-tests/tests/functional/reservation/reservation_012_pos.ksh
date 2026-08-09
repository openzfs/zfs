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
# A reservation guarantees a certain amount of space for a dataset.
# Nothing else which happens in the same pool should affect that
# space, i.e. even if the rest of the pool fills up the reserved
# space should still be accessible.
#
# STRATEGY:
# 1) Create 2 filesystems
# 2) Set a reservation on one filesystem
# 3) Fill up the other filesystem (which does not have a reservation
# set) until all space is consumed
# 4) Verify can still write to the filesystem which has a reservation
#

verify_runnable "both"

log_assert "Verify reservations protect space"

function cleanup
{
	log_must zfs destroy -f $TESTPOOL/$TESTFS2
	log_must zero_reservation $TESTPOOL/$TESTFS

	[[ -e $TESTDIR/$TESTFILE2 ]] && log_must rm -rf $TESTDIR/$TESTFILE2
	[[ -d $TESTDIR2 ]] && log_must rm -rf $TESTDIR2
}

log_onexit cleanup

log_must zfs create $TESTPOOL/$TESTFS2
log_must zfs set mountpoint=$TESTDIR2 $TESTPOOL/$TESTFS2

space_avail=`get_prop available $TESTPOOL`

((resv_size_set = space_avail - RESV_FREE_SPACE))

log_must zfs set reservation=$resv_size_set $TESTPOOL/$TESTFS

((write_count = (RESV_FREE_SPACE + RESV_TOLERANCE) / BLOCK_SIZE))

file_write -o create -f $TESTDIR2/$TESTFILE1 -b $BLOCK_SIZE -c $write_count \
    -d 0
ret=$?
if [[ $ret != $ENOSPC ]]; then
	log_fail "Did not get ENOSPC (got $ret) for non-reserved filesystem"
fi

((write_count = (RESV_FREE_SPACE - RESV_TOLERANCE) / BLOCK_SIZE))
log_must file_write -o create -f $TESTDIR/$TESTFILE2 -b $BLOCK_SIZE -c \
    $write_count -d 0

log_pass "Reserved space preserved correctly"
