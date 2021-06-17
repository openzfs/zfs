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
# Reservations (if successfully set) guarantee a minimum amount of space
# for a dataset. Unlike quotas however there should be no restrictions
# on accessing space outside of the limits of the reservation (if the
# space is available in the pool). Verify that in a filesystem with a
# reservation set that it's possible to create files both within the
# reserved space and also outside.
#
# STRATEGY:
# 1) Create a filesystem
# 2) Get the space used and available in the pool
# 3) Set a reservation on the filesystem
# 4) Verify can write a file that is bigger than the reserved space
#
# i.e. we start writing within the reserved region and then continue
# for 20MB outside it.
#

verify_runnable "both"

function cleanup
{
	[[ -e $TESTDIR/$TESTFILE1 ]] && log_must rm -rf $TESTDIR/$TESTFILE1
	log_must zfs set reservation=none $TESTPOOL/$TESTFS
}

log_onexit cleanup

log_assert "Verify can create files both inside and outside reserved areas"

space_used=`get_prop used $TESTPOOL`

log_must zfs set reservation=$RESV_SIZE $TESTPOOL/$TESTFS

#
# Calculate how many writes of BLOCK_SIZE it would take to fill
# up RESV_SIZE + 20971520 (20 MB).
#
fill_size=`expr $RESV_SIZE + 20971520`
write_count=`expr $fill_size / $BLOCK_SIZE`

log_must file_write -o create -f $TESTDIR/$TESTFILE1 -b $BLOCK_SIZE \
    -c $write_count -d 0

log_pass "Able to create files inside and outside reserved area"
