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
# Copyright 2017, loli10K. All rights reserved.
# Copyright (c) 2020 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_create/zpool_create.shlib

#
# DESCRIPTION:
#
# zpool set can modify 'ashift' property
#
# STRATEGY:
# 1. Create a pool
# 2. Verify that we can set 'ashift' only to allowed values on that pool
#

verify_runnable "global"

function cleanup
{
	log_must set_tunable64 VDEV_FILE_PHYSICAL_ASHIFT $orig_ashift
	destroy_pool $TESTPOOL1
	rm -f $disk
}

typeset goodvals=("0" "9" "10" "11" "12" "13" "14" "15" "16")
typeset badvals=("off" "on" "1" "8" "17" "1b" "ff" "-")

log_onexit cleanup

log_assert "zpool set can modify 'ashift' property"

orig_ashift=$(get_tunable VDEV_FILE_PHYSICAL_ASHIFT)
#
# Set the file vdev's ashift to the max. Overriding
# the ashift using the -o ashift property should still
# be honored.
#
log_must set_tunable64 VDEV_FILE_PHYSICAL_ASHIFT 16

disk=$TEST_BASE_DIR/disk
log_must mkfile $SIZE $disk
log_must zpool create $TESTPOOL1 $disk

for ashift in ${goodvals[@]}
do
	log_must zpool set ashift=$ashift $TESTPOOL1
	typeset value=$(get_pool_prop ashift $TESTPOOL1)
	if [[ "$ashift" != "$value" ]]; then
		log_fail "'zpool set' did not update ashift value to $ashift "\
		    "(current = $value)"
	fi
done

for ashift in ${badvals[@]}
do
	log_mustnot zpool set ashift=$ashift $TESTPOOL1
	typeset value=$(get_pool_prop ashift $TESTPOOL1)
	if [[ "$ashift" == "$value" ]]; then
		log_fail "'zpool set' incorrectly set ashift value to $value"
	fi
done

log_pass "zpool set can modify 'ashift' property"
