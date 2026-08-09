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
# DESCRIPTION:
#	Check the project used size and quota in zfs projectspace
#
#
# STRATEGY:
#	1. set zfs defaultprojectquota to a fs
#	2. write some data to the fs with specified project and size
#	3. use zfs projectspace to check the used size and quota size
#

function cleanup
{
	datasetexists $snapfs && destroy_dataset $snapfs

	log_must cleanup_projectquota
}

if ! lsattr -pd > /dev/null 2>&1; then
	log_unsupported "Current e2fsprogs does not support set/show project ID"
fi

log_onexit cleanup

log_assert "Check the zfs projectspace used and quota"

log_must zfs set defaultprojectquota=100m $QFS

mkmount_writable $QFS
log_must user_run $PUSER mkdir $PRJDIR
log_must chattr +P -p $PRJID1 $PRJDIR
log_must user_run $PUSER mkfile 50m $PRJDIR/qf
sync_all_pools

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
