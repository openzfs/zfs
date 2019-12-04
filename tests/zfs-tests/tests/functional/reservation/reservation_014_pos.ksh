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
		datasetexists $obj && log_must zfs destroy -f $obj
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
