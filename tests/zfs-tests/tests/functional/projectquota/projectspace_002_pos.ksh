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
# Copyright (c) 2017 by Fan Yong. All rights reserved.
#

. $STF_SUITE/tests/functional/projectquota/projectquota_common.kshlib

#
# DESCRIPTION:
#	Check the project used size and quota in zfs projectspace
#
#
# STRATEGY:
#	1. set zfs projectquota to a fs
#	2. write some data to the fs with specified project and size
#	3. use zfs projectspace to check the used size and quota size
#

function cleanup
{
	if datasetexists $snapfs; then
		log_must zfs destroy $snapfs
	fi

	log_must cleanup_projectquota
}

if ! lsattr -pd > /dev/null 2>&1; then
	log_unsupported "Current e2fsprogs does not support set/show project ID"
fi

log_onexit cleanup

log_assert "Check the zfs projectspace used and quota"

log_must zfs set projectquota@$PRJID1=100m $QFS

mkmount_writable $QFS
log_must user_run $PUSER mkdir $PRJDIR
log_must chattr +P -p $PRJID1 $PRJDIR
log_must user_run $PUSER mkfile 50m $PRJDIR/qf
sync

typeset snapfs=$QFS@snap

log_must zfs snapshot $snapfs

log_must eval "zfs projectspace $QFS >/dev/null 2>&1"
log_must eval "zfs projectspace $snapfs >/dev/null 2>&1"

for fs in "$QFS" "$snapfs"; do
	log_note "check the quota size in zfs projectspace $fs"
	log_must eval "zfs projectspace $fs | grep $PRJID1 | grep 100M"

	log_note "check the project used size in zfs projectspace $fs"
	log_must eval "zfs projectspace $fs | grep $PRJID1 | grep 50\\.\*M"
done

log_pass "Check the zfs projectspace used and quota"
