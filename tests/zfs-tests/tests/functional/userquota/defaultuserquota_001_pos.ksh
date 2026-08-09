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
