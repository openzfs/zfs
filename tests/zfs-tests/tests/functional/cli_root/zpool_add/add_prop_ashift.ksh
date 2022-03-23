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
# Copyright 2017, loli10K. All rights reserved.
# Copyright (c) 2020 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_create/zpool_create.shlib

#
# DESCRIPTION:
#	'zpool add' should use the ashift pool property value as default.
#
# STRATEGY:
#	1. Create a pool with default values.
#	2. Verify 'zpool add' uses the ashift pool property value when adding
#	   a new device.
#	3. Verify the default ashift value can still be overridden by manually
#	   specifying '-o ashift=<n>' from the command line.
#

verify_runnable "global"

function cleanup
{
	log_must set_tunable64 VDEV_FILE_PHYSICAL_ASHIFT $orig_ashift
	poolexists $TESTPOOL && destroy_pool $TESTPOOL
	log_must rm -f $disk1 $disk2
}

log_assert "'zpool add' uses the ashift pool property value as default."
log_onexit cleanup

disk1=$TEST_BASE_DIR/disk1
disk2=$TEST_BASE_DIR/disk2
log_must mkfile $SIZE $disk1
log_must mkfile $SIZE $disk2

orig_ashift=$(get_tunable VDEV_FILE_PHYSICAL_ASHIFT)
#
# Set the file vdev's ashift to the max. Overriding
# the ashift using the -o ashift property should still
# be honored.
#
log_must set_tunable64 VDEV_FILE_PHYSICAL_ASHIFT 16

typeset ashifts=("9" "10" "11" "12" "13" "14" "15" "16")
for ashift in ${ashifts[@]}
do
	log_must zpool create -o ashift=$ashift $TESTPOOL $disk1
	log_must zpool add $TESTPOOL $disk2
	log_must verify_ashift $disk2 $ashift

	# clean things for the next run
	log_must zpool destroy $TESTPOOL
	log_must zpool labelclear $disk1
	log_must zpool labelclear $disk2
done

for ashift in ${ashifts[@]}
do
	for cmdval in ${ashifts[@]}
	do
		log_must zpool create -o ashift=$ashift $TESTPOOL $disk1
		log_must zpool add -o ashift=$cmdval $TESTPOOL $disk2
		log_must verify_ashift $disk2 $cmdval

		# clean things for the next run
		log_must zpool destroy $TESTPOOL
		log_must zpool labelclear $disk1
		log_must zpool labelclear $disk2
	done
done

log_pass "'zpool add' uses the ashift pool property value."
