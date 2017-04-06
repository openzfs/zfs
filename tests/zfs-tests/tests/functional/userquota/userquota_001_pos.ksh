#!/bin/ksh -p
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
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
