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
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/userquota/userquota_common.kshlib

#
# DESCRIPTION:
#
#      userquota/groupquota can be set beyond the fs quota
#      userquota/groupquota can be set at a smaller size than its current usage.
#
# STRATEGY:
#       1. set quota to a fs and set a larger size of userquota and groupquota
#       2. write some data to the fs and set a smaller userquota and groupquota
#

function cleanup
{
	log_must cleanup_quota
	log_must zfs set quota=none $QFS
}

log_onexit cleanup

log_assert "Check set user|group quota to larger than the quota size of a fs"

log_must zfs set quota=200m $QFS
log_must zfs set userquota@$QUSER1=500m $QFS
log_must zfs set groupquota@$QGROUP=600m $QFS

log_must zfs get userquota@$QUSER1 $QFS
log_must zfs get groupquota@$QGROUP $QFS

log_note "write some data to the $QFS"
mkmount_writable $QFS
log_must user_run $QUSER1 mkfile 100m $QFILE
sync_all_pools

log_note "set user|group quota at a smaller size than it current usage"
log_must zfs set userquota@$QUSER1=90m $QFS
log_must zfs set groupquota@$QGROUP=90m $QFS

log_must zfs get userquota@$QUSER1 $QFS
log_must zfs get groupquota@$QGROUP $QFS

log_pass "set user|group quota to larger than quota size of a fs pass as expect"
