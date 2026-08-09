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
