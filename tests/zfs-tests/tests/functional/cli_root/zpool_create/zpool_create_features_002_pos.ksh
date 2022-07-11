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
# Copyright (c) 2012, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_create/zpool_create.shlib

################################################################################
#
#  When using the '-d' option or specifying '-o version=X' new pools should
#  have all features disabled.
#
#  1. Create a new pool with '-d'.
#  2. Verify that every feature@ property is in the 'disabled' state
#  3. Destroy pool and re-create with -o version=28
#  4. Verify again.
#
################################################################################

verify_runnable "global"

function cleanup
{
	datasetexists $TESTPOOL && log_must zpool destroy $TESTPOOL
}

function check_features
{
	for prop in $(zpool get all $TESTPOOL | awk '$2 ~ /feature@/ { print $2 }'); do
		state=$(zpool list -Ho "$prop" $TESTPOOL)
                if [[ "$state" != "disabled" ]]; then
			log_fail "$prop is enabled on new pool"
	        fi
	done
}

log_onexit cleanup

log_assert "'zpool create -d' creates pools with all features disabled"

log_must zpool create -f -d $TESTPOOL $DISKS
check_features
log_must zpool destroy -f $TESTPOOL

log_must zpool create -f -o version=28 $TESTPOOL $DISKS
check_features

log_pass
