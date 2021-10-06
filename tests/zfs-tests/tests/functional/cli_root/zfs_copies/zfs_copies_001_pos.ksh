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
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zfs_copies/zfs_copies.kshlib

#
# DESCRIPTION:
#	Verify "copies" property can be correctly set as 1,2 and 3 and different
#	filesystem can have different value of "copies" property within the same pool.
#	For object storage, verify copies can be set to 1.
#
# STRATEGY:
#	1. Create different filesystems with copies set as 1,2,3.
#	   For object storage, create filesystems with copies set to 1.
#	2. Verify that the "copies" property has been set correctly
#

verify_runnable "both"

function cleanup
{
	typeset ds

	for ds in $fs1 $fs2 $vol1 $vol2; do
		if datasetexists $ds; then
			log_must zfs destroy $ds
		fi
	done
}

log_assert "Verify 'copies' property with correct arguments works or not."
log_onexit cleanup

fs=$TESTPOOL/$TESTFS
fs1=$TESTPOOL/$TESTFS1
vol=$TESTPOOL/$TESTVOL
vol1=$TESTPOOL/$TESTVOL1

function test_zfs_create_with_copies
{
	typeset val="$1"

	log_must zfs create -o copies=$val $fs1
	if is_global_zone; then
		log_must zfs create -V $VOLSIZE -o copies=$val $vol1
		block_device_wait
	else
		log_must zfs create -o copies=$val $vol1
	fi
	for ds in $fs1 $vol1; do
		cmp_prop $ds $val
	done

	for ds in $fs1 $vol1; do
		log_must zfs destroy $ds
		block_device_wait
	done
}

function test_zfs_set_with_copies
{
	typeset val="$1"

	for ds in $fs $vol; do
		log_must zfs set copies=$val $ds
		cmp_prop $ds $val
	done
}

#
# Check the default value for copies property
#
for ds in $fs $vol; do
	cmp_prop $ds 1
done

if use_object_store; then
	test_zfs_create_with_copies 1
	test_zfs_set_with_copies 1
else
	for val in 1 2 3; do
		test_zfs_create_with_copies $val
	done

	for val in 3 2 1; do
		test_zfs_set_with_copies $val
	done
fi

log_pass "'copies' property with correct arguments works as expected. "
