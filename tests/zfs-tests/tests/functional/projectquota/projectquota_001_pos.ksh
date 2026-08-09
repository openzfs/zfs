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
#
# DESCRIPTION:
#	Check the basic function of the project{obj}quota
#
#
# STRATEGY:
#	1. Set projectquota and overwrite the quota size.
#	2. The write operation should fail with Disc quota exceeded
#	3. Set projectobjquota and overcreate the quota size.
#	4. More create should fail with Disc quota exceeded
#	5. More chattr to such project should fail with Disc quota exceeded
#

function cleanup
{
	cleanup_projectquota
}

if ! lsattr -pd > /dev/null 2>&1; then
	log_unsupported "Current e2fsprogs does not support set/show project ID"
fi

log_onexit cleanup

log_assert "If operation overwrite project{obj}quota size, it will fail"

mkmount_writable $QFS

log_note "Check the projectquota@$PRJID1"
log_must user_run $PUSER mkdir $PRJDIR
log_must chattr +P -p $PRJID1 $PRJDIR

log_must zfs set projectquota@$PRJID1=$PQUOTA_LIMIT $QFS
log_must user_run $PUSER mkfile $PQUOTA_LIMIT $PRJDIR/qf
sync_pool
log_mustnot user_run $PUSER mkfile 1 $PRJDIR/of

log_must rm -rf $PRJDIR

log_note "Check the projectobjquota@$PRJID2"
log_must zfs set xattr=sa $QFS
log_must user_run $PUSER mkdir $PRJDIR
log_must chattr +P -p $PRJID2 $PRJDIR

log_must zfs set projectobjquota@$PRJID2=$PQUOTA_OBJLIMIT $QFS
log_must user_run $PUSER mkfiles $PRJDIR/qf_ $((PQUOTA_OBJLIMIT - 1))
sync_pool
log_mustnot user_run $PUSER mkfile 1 $PRJDIR/of

log_must user_run $PUSER touch $PRJFILE
log_must user_run $PUSER chattr -p 123 $PRJFILE
log_mustnot user_run $PUSER chattr -p $PRJID2 $PRJFILE

log_pass "Operation overwrite project{obj}quota size failed as expect"
