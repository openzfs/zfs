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
# Copyright (c) 2012 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

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
	destroy_pool -f $TESTPOOL
}

function check_features
{
	for prop in $($ZPOOL get all $TESTPOOL | $AWK '$2 ~ /feature@/ { print $2 }'); do
		state=$($ZPOOL list -Ho "$prop" $TESTPOOL)
                if [[ "$state" != "disabled" ]]; then
			log_fail "$prop is enabled on new pool"
	        fi
	done
}

log_onexit cleanup

log_assert "'zpool create -d' creates pools with all features disabled"

log_must $ZPOOL create -f -d $TESTPOOL $DISKS
check_features
destroy_pool -f $TESTPOOL

log_must $ZPOOL create -f -o version=28 $TESTPOOL $DISKS
check_features

log_pass
