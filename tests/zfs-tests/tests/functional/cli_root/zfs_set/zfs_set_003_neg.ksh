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
# Copyright (c) 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/cli_root/zfs_set/zfs_set_common.kshlib

#
# DESCRIPTION:
# 'zfs set mountpoint/sharenfs' should fail when the mountpoint is invlid
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
	if datasetexists $TESTPOOL/foo; then
		log_must zfs destroy $TESTPOOL/foo
	fi
}

log_assert "'zfs set mountpoint/sharenfs' fails with invalid scenarios"
log_onexit cleanup

badpath=/tmp/foo1.$$
touch $badpath
longpath=$(gen_dataset_name 1030 "abcdefg")

log_must zfs create -o mountpoint=legacy $TESTPOOL/foo

# Do the negative testing about "property may be set but unable to remount filesystem"
log_mustnot eval "zfs set mountpoint=$badpath $TESTPOOL/foo >/dev/null 2>&1"

# Do the negative testing about "property may be set but unable to reshare filesystem"
log_mustnot eval "zfs set sharenfs=on $TESTPOOL/foo >/dev/null 2>&1"

# Do the negative testing about "sharenfs property can not be set to null"
log_mustnot eval "zfs set sharenfs= $TESTPOOL/foo >/dev/null 2>&1"

# Do the too long pathname testing (>1024)
log_mustnot eval "zfs set mountpoint=/$longpath $TESTPOOL/foo >/dev/null 2>&1"

log_pass "'zfs set mountpoint/sharenfs' fails with invalid scenarios as expected."
