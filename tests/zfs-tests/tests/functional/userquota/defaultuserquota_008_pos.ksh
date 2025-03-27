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
#       Check defaultuserquota and defaultgroupquota being exceeded at the same time
#
#
# STRATEGY:
#       1. Set defaultuserquota and defaultgroupquota to a fs
#       2. write to exceed the defaultuserquota size to check the result
#	3. unset defaultuserquota
#       4. write (as a different user) to exceed the defaultgroupquota size to check the result
#

function cleanup
{
	cleanup_quota
}

log_onexit cleanup

log_assert "write in excess of any default{user|group}quota size fails"

log_note "write to $QFS to make it exceed defaultuserquota ($GQUOTA_SIZE)"
log_must zfs set defaultuserquota=$GQUOTA_SIZE $QFS
log_must zfs set defaultgroupquota=$GQUOTA_SIZE $QFS

mkmount_writable $QFS
log_must user_run $QUSER1 mkfile $GQUOTA_SIZE $QFILE
sync_pool

log_must eval "zfs get -p userused@$QUSER1 $QFS >/dev/null 2>&1"
log_must eval "zfs get -p groupused@$GROUPUSED $QFS >/dev/null 2>&1"

log_mustnot user_run $QUSER1 mkfile 1 $OFILE

log_must zfs set defaultuserquota=none $QFS

log_note "write to $QFS as $QUSER2 to make it exceed defaultgroupquota"
log_mustnot user_run $QUSER2 mkfile 1 $QFILE

log_must eval "zfs get -p userused@$QUSER1 $QFS >/dev/null 2>&1"
log_must eval "zfs get -p userused@$QUSER2 $QFS >/dev/null 2>&1"
log_must eval "zfs get -p groupused@$GROUPUSED $QFS >/dev/null 2>&1"

log_pass "write in excess of any default{user|group}quota size failed as expected"
