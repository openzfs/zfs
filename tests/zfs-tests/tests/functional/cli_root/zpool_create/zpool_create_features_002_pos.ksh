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
