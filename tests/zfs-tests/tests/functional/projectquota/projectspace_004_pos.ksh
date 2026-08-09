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
# Copyright (c) 2017 by Fan Yong. Fan rights reserved.
#

. $STF_SUITE/tests/functional/projectquota/projectquota_common.kshlib

#
# DESCRIPTION:
#	Check 'df' command on the directory with INHERIT (project ID) flag
#
#
# STRATEGY:
#	1. set project [obj]quota on the directory
#	2. set project ID and inherit flag on the directory
#	3. run 'df [-i]' on the directory and check the result
#

function cleanup
{
	datasetexists $snap_fs && destroy_dataset $snap_fs

	log_must cleanup_projectquota
}

if ! lsattr -pd > /dev/null 2>&1; then
	log_unsupported "Current e2fsprogs does not support set/show project ID"
fi

log_onexit cleanup

log_assert "Check 'df' on dir with inherit project shows the project quota/used"

log_must zfs set projectquota@$PRJID1=100m $QFS
log_must zfs set projectobjquota@$PRJID1=100 $QFS
mkmount_writable $QFS
log_must user_run $PUSER mkdir $PRJDIR
log_must chattr +P -p $PRJID1 $PRJDIR
log_must user_run $PUSER mkfile 50m $PRJDIR/qf
sync_pool

total=$(df $PRJDIR | awk 'END { print $2 }')
[[ $total -eq 102400 ]] || log_fail "expect '102400' resource, but got '$total'"

used=$(df -i $PRJDIR | awk 'END { print $5 }')
[[ "$used" == "2%" ]] || log_fail "expect '2%' used, but got '$used'"

log_pass "'df' on the directory with inherit project ID flag pass as expect"
