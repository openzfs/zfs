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
# Setting a reservation on dataset should have no effect on any other
# dataset at the same level in the hierarchy beyond using up available
# space in the pool.
#
# STRATEGY:
# 1) Create a filesystem
# 2) Set a reservation on the filesystem
# 3) Create another filesystem at the same level
# 4) Set a reservation on the second filesystem
# 5) Destroy both the filesystems
# 6) Verify space accounted for correctly
#

verify_runnable "both"

log_assert "Verify reservations on data sets doesn't affect other data sets " \
    "at same level except for consuming space from common pool"

function cleanup
{
	datasetexists $TESTPOOL/$TESTFS2 && \
		destroy_dataset $TESTPOOL/$TESTFS2 -f

	datasetexists $TESTPOOL/$TESTFS1 && \
		destroy_dataset $TESTPOOL/$TESTFS1 -f
}

log_onexit cleanup

space_avail=`get_prop available $TESTPOOL`
space_used=`get_prop used $TESTPOOL`

resv_size_set=`expr $space_avail / 3`

#
# Function which creates two datasets, sets reservations on them,
# then destroys them and ensures that space is correctly accounted
# for.
#
# Any special arguments for create are passed in via the args
# parameter.
#
function create_resv_destroy { # args1 dataset1 args2 dataset2

	args1=$1
	dataset1=$2
	args2=$3
	dataset2=$4

	log_must zfs create $args1 $dataset1

	log_must zfs set reservation=$RESV_SIZE $dataset1

	avail_aft_dset1=`get_prop available $TESTPOOL`
	used_aft_dset1=`get_prop used $TESTPOOL`

	log_must zfs create $args2 $dataset2

	log_must zfs set reservation=$RESV_SIZE $dataset2

	#
	# After destroying the second dataset the space used and
	# available totals should revert back to the values they
	# had after creating the first dataset.
	#
	log_must_busy zfs destroy -f $dataset2

	avail_dest_dset2=`get_prop available $TESTPOOL`
	used_dest_dset2=`get_prop used $TESTPOOL`

	log_must within_limits $avail_aft_dset1 $avail_dest_dset2 $RESV_TOLERANCE
	log_must within_limits $used_aft_dset1 $used_dest_dset2 $RESV_TOLERANCE


	# After destroying the first dataset the space used and
	# space available totals should revert back to the values
	# they had when the pool was first created.
	log_must_busy zfs destroy -f $dataset1

	avail_dest_dset1=`get_prop available $TESTPOOL`
	used_dest_dset1=`get_prop used $TESTPOOL`

	log_must within_limits $avail_dest_dset1 $space_avail $RESV_TOLERANCE
	log_must within_limits $used_dest_dset1 $space_used $RESV_TOLERANCE
}

create_resv_destroy "" $TESTPOOL/$TESTFS1 ""  $TESTPOOL/$TESTFS2
create_resv_destroy "" $TESTPOOL/$TESTFS2 "" $TESTPOOL/$TESTFS1

log_pass "Verify reservations on data sets doesn't affect other data sets at" \
	" same level except for consuming space from common pool"
