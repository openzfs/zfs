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
#
# DESCRIPTION:
#       Check the basic function of defaultuserquota and defaultgroupquota
#
#
# STRATEGY:
#       1. Set defaultuserquota and exceed the quota size
#       2. The write operation should fail with "Disk quota exceeded"
#       3. Set defaultgroupquota and exceed the quota size
#       4. The write operation should fail with "Disk quota exceeded"
#
#

function cleanup
{
	cleanup_quota
}

log_onexit cleanup

log_assert "If write operation exceeds default{user|group}quota size, it will fail"

mkmount_writable $QFS
log_note "Check the defaultuserquota"
log_must zfs set defaultuserquota=$UQUOTA_SIZE $QFS
log_must user_run $QUSER1 mkfile $UQUOTA_SIZE $QFILE
sync_pool
log_mustnot user_run $QUSER1 mkfile 1 $OFILE
cleanup_quota

log_note "Check the defaultgroupquota"
log_must zfs set defaultgroupquota=$GQUOTA_SIZE $QFS
mkmount_writable $QFS
log_must user_run $QUSER1 mkfile $GQUOTA_SIZE $QFILE
sync_pool
log_mustnot user_run $QUSER1 mkfile 1 $OFILE
log_mustnot user_run $QUSER2 mkfile 1 $OFILE
cleanup_quota

log_pass "Write operation exceeded default{user|group}quota size, failed as expected"
