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
# A reservation cannot exceed the quota on a dataset
#
# STRATEGY:
# 1) Create a filesystem and volume
# 2) Set a quota on the filesystem
# 3) Attempt to set a reservation larger than the quota. Verify
# that the attempt fails.
# 4) Repeat 2-3 for volume
#

verify_runnable "both"

log_assert "Verify cannot set reservation larger than quota"

function cleanup
{
	#
	# Note we don't destroy $TESTFS as it's used by other tests
	for obj in $OBJ_LIST ; do
		datasetexists $obj && destroy_dataset $obj -f
	done

	log_must zero_reservation $TESTPOOL/$TESTFS
}
log_onexit cleanup

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
	log_must zfs create -s -V $sparse_vol_set_size $TESTPOOL/$TESTVOL2
fi

for obj in $TESTPOOL/$TESTFS $OBJ_LIST ; do

	space_avail=`get_prop available $obj`
	((quota_set_size = space_avail / 3))

	#
	# Volumes do not support quota so only need to explicitly
	# set quotas for filesystems.
	#
	# The maximum reservation value that can be set on a volume
	# is determined by the quota set on its parent filesystems or
	# the amount of space in the pool, whichever is smaller.
	#
	if [[ $obj == $TESTPOOL/$TESTFS ]]; then
		log_must zfs set quota=$quota_set_size $obj
		((resv_set_size = quota_set_size + RESV_SIZE))
	elif [[ $obj == $TESTPOOL/$TESTVOL || $obj == $TESTPOOL/$TESTVOL2 ]]
	then
		resv_set_size=`expr $space_avail + $RESV_DELTA`
	fi

	orig_quota=`get_prop quota $obj`

	log_mustnot zfs set reservation=$resv_set_size $obj
	new_quota=`get_prop quota $obj`

	if [[ $orig_quota != $new_quota ]]; then
		log_fail "Quota value changed from $orig_quota " \
				"to $new_quota"
	fi

	if [[ $obj == $TESTPOOL/$TESTFS ]]; then
		log_must zfs set quota=none $obj
	fi
done

log_pass "As expected cannot set reservation larger than quota"
