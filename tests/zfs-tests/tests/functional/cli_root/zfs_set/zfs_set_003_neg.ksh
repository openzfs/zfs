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
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/cli_root/zfs_set/zfs_set_common.kshlib

#
# DESCRIPTION:
# 'zfs set mountpoint/sharenfs' should set the property when mountpoint
#  is invalid. Setting the property should be successful, but dataset
# should not be mounted, as mountpoint is invalid.
#
# STRATEGY:
# 1. Create invalid scenarios
# 2. Run zfs set mountpoint/sharenfs with invalid value
# 3. Verify that zfs set returns expected errors
#

verify_runnable "both"

function cleanup
{
	if [ -e $badpath ]; then
		rm -f $badpath
	fi

	datasetexists $TESTPOOL/foo && destroy_dataset $TESTPOOL/foo
}

log_assert "'zfs set mountpoint/sharenfs' fails with invalid scenarios"
log_onexit cleanup

badpath=$TEST_BASE_DIR/foo1.$$
touch $badpath
longpath=$(gen_dataset_name 1030 "abcdefg")

log_must zfs create -o mountpoint=legacy $TESTPOOL/foo

# Do the negative testing about "property may be set but unable to remount filesystem"
set_n_check_prop "$badpath" "mountpoint" "$TESTPOOL/foo"
log_mustnot ismounted $TESTPOOL/foo

# Do the negative testing about "property may be set but unable to reshare filesystem"
set_n_check_prop "on" "sharenfs" "$TESTPOOL/foo"
log_mustnot ismounted $TESTPOOL/foo

# Do the negative testing about "sharenfs property can not be set to null"
log_mustnot eval "zfs set sharenfs= $TESTPOOL/foo >/dev/null 2>&1"

# Do the too long pathname testing (>1024)
log_mustnot eval "zfs set mountpoint=/$longpath $TESTPOOL/foo >/dev/null 2>&1"

log_pass "'zfs set mountpoint/sharenfs' fails with invalid scenarios as expected."
