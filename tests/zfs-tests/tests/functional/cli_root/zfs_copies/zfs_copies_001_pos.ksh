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
. $STF_SUITE/tests/functional/cli_root/zfs_copies/zfs_copies.kshlib

#
# DESCRIPTION:
#	Verify "copies" property can be correctly set as 1,2 and 3 and different
#	filesystem can have different value of "copies" property within the same pool.
#
# STRATEGY:
#	1. Create different filesystems with copies set as 1,2,3;
#	2. Verify that the "copies" property has been set correctly
#

verify_runnable "both"

function cleanup
{
	typeset ds

	for ds in $fs1 $fs2 $vol1 $vol2; do
		datasetexists $ds && destroy_dataset $ds
	done
}

log_assert "Verify 'copies' property with correct arguments works or not."
log_onexit cleanup

fs=$TESTPOOL/$TESTFS
fs1=$TESTPOOL/$TESTFS1
fs2=$TESTPOOL/$TESTFS2
vol=$TESTPOOL/$TESTVOL
vol1=$TESTPOOL/$TESTVOL1
vol2=$TESTPOOL/$TESTVOL2

#
# Check the default value for copies property
#
for ds in $fs $vol; do
	cmp_prop $ds 1
done

for val in 1 2 3; do
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

	for val2 in 3 2 1; do
		log_must zfs create -o copies=$val2 $fs2
		if is_global_zone; then
			log_must zfs create -V $VOLSIZE -o copies=$val2 $vol2
			block_device_wait
		else
			log_must zfs create -o copies=$val2 $vol2
		fi
		for ds in $fs2 $vol2; do
			cmp_prop $ds $val2
			destroy_dataset $ds
			block_device_wait
		done
	done

	for ds in $fs1 $vol1; do
		destroy_dataset $ds
		block_device_wait
	done

done

for val in 3 2 1; do
	for ds in $fs $vol; do
		log_must zfs set copies=$val $ds
		cmp_prop $ds $val
	done
done

log_pass "'copies' property with correct arguments works as expected. "
