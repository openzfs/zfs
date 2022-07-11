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
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zfs_upgrade/zfs_upgrade.kshlib

#
# DESCRIPTION:
# 'zfs create -p -o version=1' should only cause the leaf filesystem to be version=1
#
# STRATEGY:
# 1. Create $newdataset with -p option, verify it is created
# 2. Verify only the leaf filesystem to be version=1, others use the current version
#

ZFS_VERSION=$(zfs upgrade | grep -wom1 '[[:digit:]]*')

verify_runnable "both"

function cleanup
{
	datasetexists $TESTPOOL/$TESTFS1 && \
		destroy_dataset $TESTPOOL/$TESTFS1 -rf
}

log_onexit cleanup


typeset newdataset1="$TESTPOOL/$TESTFS1/$TESTFS/$TESTFS1"

log_assert "'zfs create -p -o version=1' only cause the leaf filesystem to be version=1."

log_must zfs create -p -o version=1 $newdataset1
log_must datasetexists $newdataset1

log_must check_fs_version $TESTPOOL/$TESTFS1/$TESTFS/$TESTFS1 1
for fs in $TESTPOOL/$TESTFS1 $TESTPOOL/$TESTFS1/$TESTFS ; do
	log_must check_fs_version $fs $ZFS_VERSION
done

log_pass "'zfs create -p -o version=1' only cause the leaf filesystem to be version=1."
