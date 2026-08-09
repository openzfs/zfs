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
#       Check the basic function of the userquota and groupquota
#
#
# STRATEGY:
#       1. Set userquota and overwrite the quota size
#       2. The write operation should fail with Disc quota exceeded
#       3. Set groupquota and overwrite the quota size
#       4. The write operation should fail with Disc quota exceeded
#
#

function cleanup
{
	cleanup_quota
}

log_onexit cleanup

log_assert "If write operation overwrite {user|group}quota size, it will fail"

mkmount_writable $QFS
log_note "Check the userquota@$QUSER1"
log_must zfs set userquota@$QUSER1=$UQUOTA_SIZE $QFS
log_must user_run $QUSER1 mkfile $UQUOTA_SIZE $QFILE
sync_pool
log_mustnot user_run $QUSER1 mkfile 1 $OFILE
cleanup_quota

log_note "Check the groupquota@$QGROUP"
log_must zfs set groupquota@$QGROUP=$GQUOTA_SIZE $QFS
mkmount_writable $QFS
log_must user_run $QUSER1 mkfile $GQUOTA_SIZE $QFILE
sync_pool
log_mustnot user_run $QUSER1 mkfile 1 $OFILE

cleanup_quota

log_pass "Write operation overwrite {user|group}quota size, it as expect"
