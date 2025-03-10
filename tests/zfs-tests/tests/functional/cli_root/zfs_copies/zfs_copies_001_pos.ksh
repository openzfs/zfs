#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or https://opensource.org/licenses/CDDL-1.0.
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
