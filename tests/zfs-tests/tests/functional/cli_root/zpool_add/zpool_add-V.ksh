#!/bin/ksh -p
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
# Copyright (c) 2023 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_add/zpool_add.kshlib

#
# DESCRIPTION:
#	'zpool add' should only work if the ashift matches the pool
#	ashift unless the '-V' option is provided.
#
# STRATEGY:
#	1. Create a pool with default values.
#	2. Add a disk with a specific ashift
#	3. Verify that the operation is allowed if ashift match
#	4. Attempt to add a vdev with a mismatched ashift and ensure it fails
#	5. Rerun the operation but specify the -V to override the check
#

verify_runnable "global"

function cleanup
{
	log_must set_tunable32 VDEV_FILE_PHYSICAL_ASHIFT $orig_physical_ashift
	log_must set_tunable32 VDEV_FILE_LOGICAL_ASHIFT $orig_logical_ashift
	poolexists $TESTPOOL && destroy_pool $TESTPOOL
	rm -f $disk1 $disk2
}

log_assert "zpool add only works when device ashift matches pool ashift"
log_onexit cleanup

disk1=$TEST_BASE_DIR/disk1
disk2=$TEST_BASE_DIR/disk2
log_must mkfile $SIZE $disk1
log_must mkfile $SIZE $disk2

orig_logical_ashift=$(get_tunable VDEV_FILE_LOGICAL_ASHIFT)
orig_physical_ashift=$(get_tunable VDEV_FILE_PHYSICAL_ASHIFT)

typeset ashifts=("9" "10" "11" "12" "13" "14" "15" "16")
for ashift_disk1 in ${ashifts[@]}
do
	for ashift_disk2 in ${ashifts[@]}
	do
		#
		# The default case without any parameters
		#
		if [[ $ashift_disk1 -eq $ashift_disk2 ]]; then
			log_must zpool create $TESTPOOL $disk1
			log_must zpool add $TESTPOOL $disk2

			# clean things for the next run
			log_must set_tunable32 VDEV_FILE_PHYSICAL_ASHIFT $orig_physical_ashift
			log_must set_tunable32 VDEV_FILE_LOGICAL_ASHIFT $orig_logical_ashift
			log_must zpool destroy $TESTPOOL
			log_must zpool labelclear $disk1
			log_must zpool labelclear $disk2
			continue
		fi

		#
		# 1. Create a pool with a specific ashift
		# 2. Try to add a vdev with a mismatched ashift
		# 3. Override the ashift validation to add the vdev
		#
		log_must zpool create -o ashift=$ashift_disk1 $TESTPOOL $disk1
		log_must verify_ashift $disk1 $ashift_disk1
		log_mustnot zpool add -o ashift=$ashift_disk2 $TESTPOOL $disk2
		log_must zpool add -V -o ashift=$ashift_disk2 $TESTPOOL $disk2
		log_must verify_ashift $disk2 $ashift_disk2

		# clean things for the next run
		log_must zpool destroy $TESTPOOL
		log_must zpool labelclear $disk1
		log_must zpool labelclear $disk2

		#
		# Test the tunable.
		#
		log_must set_tunable32 VDEV_FILE_PHYSICAL_ASHIFT $ashift_disk1
		log_must set_tunable32 VDEV_FILE_LOGICAL_ASHIFT $ashift_disk1
		log_must zpool create $TESTPOOL $disk1
		log_must verify_ashift $disk1 $(get_tunable VDEV_FILE_LOGICAL_ASHIFT)

		log_must set_tunable32 VDEV_FILE_PHYSICAL_ASHIFT $ashift_disk2
		log_must set_tunable32 VDEV_FILE_LOGICAL_ASHIFT $ashift_disk2
		log_mustnot zpool add $TESTPOOL $disk2
		log_must zpool add -V $TESTPOOL $disk2
		log_must verify_ashift $disk2 $(get_tunable VDEV_FILE_LOGICAL_ASHIFT)

		# clean things for the next run
		log_must set_tunable32 VDEV_FILE_PHYSICAL_ASHIFT $orig_physical_ashift
		log_must set_tunable32 VDEV_FILE_LOGICAL_ASHIFT $orig_logical_ashift
		log_must zpool destroy $TESTPOOL
		log_must zpool labelclear $disk1
		log_must zpool labelclear $disk2
	done
done

log_pass "zpool add only works when device ashift matches pool ashift"
