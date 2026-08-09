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
#  Newly created pools should have all features enabled.
#  Specifying a feature to be enabled with '-o' should be a no-op.
#
#  1. Create a new pool.
#  2. Verify that every feature@ property is in the 'enabled' or 'active' state
#  3. Destroy the pool and create a new pool with
#     '-o feature@async_destroy=enabled'
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
	for state in $(zpool get all $TESTPOOL | grep -v "dynamic_gang_header" | \
	    awk '$2 ~ /feature@/ { print $3 }'); do
		if [[ "$state" != "enabled" && "$state" != "active" ]]; then
			log_fail "some features are not enabled on new pool"
	        fi
	done
}

log_onexit cleanup

log_assert "'zpool create' creates pools with all features enabled"

log_must zpool create -f $TESTPOOL $DISKS
check_features
log_must zpool destroy -f $TESTPOOL

log_must zpool create -f -o feature@async_destroy=enabled $TESTPOOL $DISKS
check_features

log_pass "'zpool create' creates pools with all features enabled"
