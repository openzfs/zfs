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
#
#      defaultuserquota/defaultgroupquota can be set beyond the fs quota
#      defaultuserquota/defaultgroupquota can be set at a smaller size than its current usage.
#
# STRATEGY:
#       1. set quota to a fs and set a larger size of defaultuserquota and defaultgroupquota
#       2. write some data to the fs and set a smaller defaultuserquota and defaultgroupquota
#

function cleanup
{
	log_must cleanup_quota
	log_must zfs set quota=none $QFS
}

log_onexit cleanup

log_assert "Check set default{user|group}quota to larger than the quota size of a fs"

log_must zfs set quota=200m $QFS
log_must zfs set defaultuserquota="$UQUOTA_SIZE" $QFS
log_must zfs set defaultgroupquota="$GQUOTA_SIZE" $QFS

log_must check_quota "defaultuserquota" $QFS "$UQUOTA_SIZE"
log_must check_quota "defaultgroupquota" $QFS "$GQUOTA_SIZE"

log_note "write some data to the $QFS"
mkmount_writable $QFS
log_must user_run $QUSER1 mkfile 100000 $QFILE
sync_all_pools

log_note "set default{user|group}quota at a smaller size than current usage"
log_must zfs set defaultuserquota=90000 $QFS
log_must zfs set defaultgroupquota=90000 $QFS

log_must check_quota "defaultuserquota" $QFS 90000
log_must check_quota "defaultgroupquota" $QFS 90000

log_pass "set default{user|group}quota to larger than quota size of a fs passed as expected"
