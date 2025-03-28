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
# Copyright (c) 2017 by Fan Yong. All rights reserved.
#

. $STF_SUITE/tests/functional/projectquota/projectquota_common.kshlib

#
#
# DESCRIPTION:
#	Check the basic function of the defaultproject{obj}quota
#
#
# STRATEGY:
#	1. Set defaultprojectquota and overwrite the quota size.
#	2. The write operation should fail.
#	3. Set defaultprojectobjquota and overcreate the quota size.
#	4. More create should fail.
#	5. More chattr to such project should fail.
#

function cleanup
{
	cleanup_projectquota
}

if ! lsattr -pd > /dev/null 2>&1; then
	log_unsupported "Current e2fsprogs does not support set/show project ID"
fi

log_onexit cleanup

log_assert "If operation overwrite defaultproject{obj}quota size, it will fail"

mkmount_writable $QFS

log_note "Check the defaultprojectquota"
log_must user_run $PUSER mkdir $PRJDIR
log_must chattr +P -p $PRJID1 $PRJDIR

log_must zfs set defaultprojectquota=$PQUOTA_LIMIT $QFS
log_must user_run $PUSER mkfile $PQUOTA_LIMIT $PRJDIR/qf
sync_pool
log_mustnot user_run $PUSER mkfile 1 $PRJDIR/of

log_must rm -rf $PRJDIR

log_note "Check the defaultprojectobjquota"
log_must zfs set xattr=sa $QFS
log_must user_run $PUSER mkdir $PRJDIR
log_must chattr +P -p $PRJID2 $PRJDIR

log_must zfs set defaultprojectobjquota=$PQUOTA_OBJLIMIT $QFS
log_must user_run $PUSER mkfiles $PRJDIR/qf_ $((PQUOTA_OBJLIMIT - 1))
sync_pool
log_mustnot user_run $PUSER mkfile 1 $PRJDIR/of

log_must user_run $PUSER touch $PRJFILE
log_must user_run $PUSER chattr -p 123 $PRJFILE
log_mustnot user_run $PUSER chattr -p $PRJID2 $PRJFILE

log_pass "Operation overwrite defaultproject{obj}quota size failed as expect"
