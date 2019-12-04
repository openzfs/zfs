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
