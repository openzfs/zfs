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

total=$(df $PRJDIR | tail -n 1 | awk '{ print $2 }')
[[ $total -eq 102400 ]] || log_fail "expect '102400' resource, but got '$total'"

used=$(df -i $PRJDIR | tail -n 1 | awk '{ print $5 }')
[[ "$used" == "2%" ]] || log_fail "expect '2%' used, but got '$used'"

log_pass "'df' on the directory with inherit project ID flag pass as expect"
