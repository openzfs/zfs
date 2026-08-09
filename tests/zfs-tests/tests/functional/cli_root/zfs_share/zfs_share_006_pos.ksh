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
# Copyright (c) 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Verify that a dataset could not be shared but filesystems are shared.
#
# STRATEGY:
# 1. Create a dataset and file system
# 2. Set the sharenfs property on the dataset
# 3. Verify that the dataset is unable be shared.
# 4. Add a new file system to the dataset.
# 5. Verify that the newly added file system be shared.
#

verify_runnable "global"

function cleanup
{
	log_must zfs set sharenfs=off $TESTPOOL/$TESTCTR
	if mounted $TESTDIR2; then
		log_must zfs unmount $TESTDIR2
	fi

	datasetexists $TESTPOOL/$TESTCTR/$TESTFS2 && \
		destroy_dataset $TESTPOOL/$TESTCTR/$TESTFS2

	typeset fs=""
	for fs in $mntp $TESTDIR1 $TESTDIR2
	do
		log_must unshare_fs $fs
	done
}

#
# Main test routine.
#
# Given a mountpoint and a dataset, this routine will set the
# sharenfs property on the dataset and verify that dataset
# is unable to be shared but the existing contained file systems
# could be shared.
#
function test_ctr_share # mntp ctr
{
	typeset mntp=$1
	typeset ctr=$2

	not_shared $mntp || \
	    log_fail "Mountpoint: $mntp is already shared."

	log_must zfs set sharenfs=on $ctr

	not_shared $mntp || \
		log_fail "File system $mntp is shared (set sharenfs)."

	#
	# Add a new file system to the dataset and verify it is shared.
	#
	typeset mntp2=$TESTDIR2
	log_must zfs create $ctr/$TESTFS2
	log_must zfs set mountpoint=$mntp2 $ctr/$TESTFS2

	is_shared $mntp2 || \
	    log_fail "File system $mntp2 was not shared (set sharenfs)."
}

log_assert "Verify that a dataset could not be shared, " \
	"but its sub-filesystems could be shared."
log_onexit cleanup

typeset mntp=$(get_prop mountpoint $TESTPOOL/$TESTCTR)
test_ctr_share $mntp $TESTPOOL/$TESTCTR

log_pass "A dataset could not be shared, " \
	"but its sub-filesystems could be shared as expect."
