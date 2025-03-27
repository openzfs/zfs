#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
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
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/userquota/userquota_common.kshlib

#
# DESCRIPTION:
#       Check default{user|group}quota to snapshot such that:
#	1) can not set default{user|group}quota to snapshot directly
#	2) snapshot does not inherit the parent fs's default{user|group}quota
#	    (same behavior as Solaris)
#
#
# STRATEGY:
#       1. create a snapshot of a fs
#       2. set the default{user|group}quota to snapshot and expect fail
#	3. set default{user|group}quota to fs and check the snapshot
#	4. reset default{user|group}quota to fs and check the snapshot's value
#

function cleanup
{
	datasetexists $snap_fs && destroy_dataset $snap_fs

	log_must cleanup_quota
}

log_onexit cleanup

log_assert "Check the snapshot's default{user|group}quota"
typeset snap_fs=$QFS@snap

log_must zfs set defaultuserquota=$UQUOTA_SIZE $QFS
log_must check_quota "defaultuserquota" $QFS "$UQUOTA_SIZE"

log_must zfs set defaultgroupquota=$GQUOTA_SIZE $QFS
log_must check_quota "defaultgroupquota" $QFS "$GQUOTA_SIZE"

log_must zfs snapshot $snap_fs

log_note "check the snapshot $snap_fs default{user|group}quota"
log_must check_quota "defaultuserquota" $snap_fs "$UQUOTA_SIZE"
log_must check_quota "defaultgroupquota" $snap_fs "$GQUOTA_SIZE"

log_note  "set default{user|group}quota to $snap_fs should fail"
log_mustnot zfs set defaultuserquota=$SNAP_QUOTA $snap_fs
log_mustnot zfs set defaultgroupquota=$SNAP_QUOTA $snap_fs

log_note "change the parent filesystem's default{user|group}quota"
log_must zfs set defaultuserquota=$TEST_QUOTA $QFS
log_must zfs set defaultgroupquota=$TEST_QUOTA $QFS

log_must check_quota "defaultuserquota" $QFS $TEST_QUOTA
log_must check_quota "defaultgroupquota" $QFS $TEST_QUOTA

log_note "check the snapshot $snap_fs default{user|group}quota"
log_must check_quota "defaultuserquota" $snap_fs "$UQUOTA_SIZE"
log_must check_quota "defaultgroupquota" $snap_fs "$GQUOTA_SIZE"

log_pass "Check the snapshot's default{user|group}quota passed as expected"
