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
# ZFS has two mechanisms dealing with space for datasets, namely
# reservations and quotas. Setting one should not affect the other,
# provided the values are legal (i.e. enough space in pool etc).
#
# STRATEGY:
# 1) Create one filesystem
# 2) Get the current quota setting
# 3) Set a reservation
# 4) Verify that the quota value remains unchanged
#

verify_runnable "both"

log_assert "Verify reservation settings do not affect quota settings"

function cleanup
{
	log_must zero_reservation $TESTPOOL/$TESTFS
}

log_onexit cleanup

space_avail=`get_prop available $TESTPOOL`

((resv_size_set = (space_avail - RESV_DELTA) / 2))

fs_quota=`zfs get quota $TESTPOOL/$TESTFS`

log_must zfs set reservation=$resv_size_set $TESTPOOL/$TESTFS

new_fs_quota=`zfs get quota $TESTPOOL/$TESTFS`

if [[ $fs_quota != $new_fs_quota ]]; then
	log_fail "Quota value on $TESTFS has changed " \
	    "($fs_quota != $new_fs_quota)"
fi

log_pass "Quota settings unaffected by reservation settings"
