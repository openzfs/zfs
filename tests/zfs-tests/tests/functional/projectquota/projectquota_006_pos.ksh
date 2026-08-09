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
# Copyright (c) 2017 by Fan Yong. All rights reserved.
#

. $STF_SUITE/tests/functional/projectquota/projectquota_common.kshlib

#
# DESCRIPTION:
#	Projectquota can be set beyond the fs quota.
#	Pprojectquota can be set at a smaller size than its current usage.
#
# STRATEGY:
#	1. set quota to a fs and set a larger size of projectquota
#	2. write some data to the fs and set a smaller projectquota
#

function cleanup
{
	log_must cleanup_projectquota
	log_must zfs set quota=none $QFS
}

if ! lsattr -pd > /dev/null 2>&1; then
	log_unsupported "Current e2fsprogs does not support set/show project ID"
fi

log_onexit cleanup

log_assert "Check set projectquota to larger than the quota size of a fs"

log_must zfs set quota=200m $QFS
log_must zfs set projectquota@$PRJID1=500m $QFS

log_must zfs get projectquota@$PRJID1 $QFS

log_note "write some data to the $QFS"
mkmount_writable $QFS
log_must user_run $PUSER mkdir $PRJDIR
log_must chattr +P -p $PRJID1 $PRJDIR
log_must user_run $PUSER mkfile 100m $PRJDIR/qf
sync_all_pools

log_note "set projectquota at a smaller size than it current usage"
log_must zfs set projectquota@$PRJID1=90m $QFS

log_must zfs get projectquota@$PRJID1 $QFS

log_pass "set projectquota to larger than quota size of a fs"
