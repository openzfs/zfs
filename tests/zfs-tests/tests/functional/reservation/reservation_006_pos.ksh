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
