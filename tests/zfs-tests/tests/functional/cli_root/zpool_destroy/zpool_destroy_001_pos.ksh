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
# Copyright (c) 2012, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_destroy/zpool_destroy.cfg


#
# DESCRIPTION:
#	'zpool destroy <pool>' can successfully destroy the specified pool.
#
# STRATEGY:
#	1. Create a storage pool
#	2. Destroy the pool
#	3. Verify the is destroyed successfully
#

verify_runnable "global"

function cleanup
{
	poolexists $TESTPOOL2 && destroy_pool $TESTPOOL2
	datasetexists $TESTPOOL1/$TESTVOL && \
		log_must zfs destroy -f $TESTPOOL1/$TESTVOL

	typeset pool
	for pool in $TESTPOOL1 $TESTPOOL; do
		poolexists $pool && destroy_pool $pool
	done

	zero_partitions $DISK
}

set -A datasets "$TESTPOOL" "$TESTPOOL2"

if ! $(is_physical_device $DISKS) ; then
	log_unsupported "This case cannot be run on raw files."
fi

log_assert "'zpool destroy <pool>' can destroy a specified pool."

log_onexit cleanup

partition_disk $SLICE_SIZE $DISK 2

create_pool "$TESTPOOL" "${DISK}${SLICE_PREFIX}${SLICE0}"
create_pool "$TESTPOOL1" "${DISK}${SLICE_PREFIX}${SLICE1}"
log_must zfs create -s -V $VOLSIZE $TESTPOOL1/$TESTVOL
block_device_wait
create_pool "$TESTPOOL2" "${ZVOL_DEVDIR}/$TESTPOOL1/$TESTVOL"

typeset -i i=0
while (( i < ${#datasets[*]} )); do
	log_must poolexists "${datasets[i]}"
	log_must zpool destroy "${datasets[i]}"
	log_mustnot poolexists "${datasets[i]}"
	((i = i + 1))
done

log_pass "'zpool destroy <pool>' executes successfully"
