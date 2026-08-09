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
# Verify that it's possible to set a reservation on a filesystem,
# or volume multiple times, without resetting the reservation
# to none.
#
# STRATEGY:
# 1) Create a regular volume and a sparse volume
# 2) Get the space available in the pool
# 3) Set a reservation on the filesystem less than the space available.
# 4) Verify that the 'reservation' property for the filesystem has
# the correct value.
# 5) Repeat 2-4 for different reservation values
# 6) Repeat 3-5 for regular and sparse volume
#

verify_runnable "both"

log_assert "Verify it is possible to set reservations multiple times " \
	"on a filesystem regular and sparse volume"

function cleanup
{
	log_must zero_reservation $TESTPOOL/$TESTFS

	for obj in $OBJ_LIST; do
	datasetexists $obj && destroy_dataset $obj -f
	done
}

log_onexit cleanup


#
# Set a reservation $RESV_ITER times on a dataset and verify that
# the reservation is correctly set each time.
#
function multiple_resv { #dataset
	typeset -i i=0

	dataset=$1

	log_must zero_reservation $dataset
	space_avail=`get_prop available $TESTPOOL`

	((resv_size = (space_avail - RESV_DELTA) / RESV_ITER))

	#
	# For regular (non-sparse) volumes the upper limit is determined
	# not by the space available in the pool but rather by the size
	# of the volume itself.
	#
	[[ $obj == $TESTPOOL/$TESTVOL ]] && \
	    ((resv_size = (vol_set_size - RESV_DELTA) / RESV_ITER))

	resv_size_set=$resv_size

	while (($i < $RESV_ITER)); do

		((i = i + 1))

		((resv_size_set = resv_size * i))

		log_must zfs set reservation=$resv_size_set $dataset

		resv_size_get=`get_prop reservation $dataset`
		if [[ $resv_size_set != $resv_size_get ]]; then
			log_fail "Reservation not the expected value " \
			    "($resv_size_set != $resv_size_get)"
		fi
	done

	log_must zero_reservation $dataset
}

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
	multiple_resv $obj
done

log_pass "Multiple reservations successfully set on filesystem" \
    " and both volume types"
