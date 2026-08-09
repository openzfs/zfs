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
# Reservation values cannot exceed the amount of space available
# in the pool. Verify that attempting to set a reservation greater
# than this value fails.
#
# STRATEGY:
# 1) Create a filesystem, regular and sparse volume
# 2) Get the space available in the pool
# 3) Attempt to set a reservation greater than the available space
# on the filesystem and verify it fails.
# 4) Verify that the reservation is still set to 'none' (or 0) on
# the filesystem.
# 5) Repeat 3-4 for regular and sparse volume
#

verify_runnable "both"

function cleanup
{
	for obj in $OBJ_LIST; do
		datasetexists $obj && destroy_dataset $obj -f
	done

	log_must zero_reservation $TESTPOOL/$TESTFS
}

log_onexit cleanup

log_assert "Reservation values cannot exceed the amount of space" \
	" available in the pool"

space_avail=`get_prop available $TESTPOOL`

if ! is_global_zone ; then
	OBJ_LIST=""
else
	OBJ_LIST="$TESTPOOL/$TESTVOL $TESTPOOL/$TESTVOL2"

	((vol_set_size = space_avail / 4))
	vol_set_size=$(floor_volsize $vol_set_size)
	((sparse_vol_set_size = space_avail * 4))
	sparse_vol_set_size=$(floor_volsize $sparse_vol_set_size)

	log_must zfs create -V $vol_set_size $TESTPOOL/$TESTVOL
	log_must zfs set reservation=none $TESTPOOL/$TESTVOL
	log_must zfs create -s -V $sparse_vol_set_size $TESTPOOL/$TESTVOL2
fi

for obj in $TESTPOOL/$TESTFS $OBJ_LIST ; do

	space_avail=`get_prop available $obj`
	resv_size_set=`expr $space_avail + $RESV_DELTA`

	log_must zero_reservation $obj
	log_mustnot zfs set reservation=$resv_size_set $obj

	resv_size_get=`get_prop reservation $obj`

	if (($resv_size_get != 0)); then
		log_fail "Reservation value non-zero ($resv_size_get)"
	fi
done

log_pass "Attempting to set too large reservation failed as expected"
