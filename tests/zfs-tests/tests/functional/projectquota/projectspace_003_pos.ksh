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
#	Check the project used object accounting in zfs projectspace
#
#
# STRATEGY:
#	1. create a bunch of files by specific project
#	2. use zfs projectspace to check the used objects
#	3. change the project ID of test files and verify object count
#	4. delete files and verify object count
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

log_assert "Check the zfs projectspace object used"

mkmount_writable $QFS
log_must zfs set xattr=sa $QFS
log_must user_run $PUSER mkdir $PRJDIR1
log_must user_run $PUSER mkdir $PRJDIR2
log_must chattr +P -p $PRJID1 $PRJDIR1
log_must chattr +P -p $PRJID2 $PRJDIR2

((prj_cnt1 = RANDOM % 100 + 2))
((prj_cnt2 = RANDOM % 100 + 2))

log_must user_run $PUSER mkfiles $PRJDIR1/qf $((prj_cnt1 - 1))
log_must user_run $PUSER mkfiles $PRJDIR2/qf $((prj_cnt2 - 1))
sync_pool

typeset snapfs=$QFS@snap

log_must zfs snapshot $snapfs

log_must eval "zfs projectspace $QFS >/dev/null 2>&1"
log_must eval "zfs projectspace $snapfs >/dev/null 2>&1"

for fs in "$QFS" "$snapfs"; do
	log_note "check the project used objects in zfs projectspace $fs"
	prjused=$(project_obj_count $fs $PRJID1)
	[[ $prjused -eq $prj_cnt1 ]] ||
		log_fail "($PRJID1) expected $prj_cnt1, got $prjused"
	prjused=$(project_obj_count $fs $PRJID2)
	[[ $prjused -eq $prj_cnt2 ]] ||
		log_fail "($PRJID2) expected $prj_cnt2, got $prjused"
done

log_note "change the project of files"
log_must chattr -p $PRJID2 $PRJDIR1/qf*
sync_pool

prjused=$(project_obj_count $QFS $PRJID1)
[[ $prjused -eq 1 ]] ||
	log_fail "expected 1 for project $PRJID1, got $prjused"

prjused=$(project_obj_count $snapfs $PRJID1)
[[ $prjused -eq $prj_cnt1 ]] ||
	log_fail "expected $prj_cnt1 for $PRJID1 in snapfs, got $prjused"

prjused=$(project_obj_count $QFS $PRJID2)
[[ $prjused -eq $((prj_cnt1 + prj_cnt2 - 1)) ]] ||
	log_fail "($PRJID2) expected $((prj_cnt1 + prj_cnt2 - 1)), got $prjused"

log_note "file removal"
log_must rm -rf $PRJDIR1
sync_pool

prjused=$(project_obj_count $QFS $PRJID1)
[[ $prjused -eq 0 ]] || log_fail "expected 0 for $PRJID1, got $prjused"

cleanup
log_pass "Check the zfs projectspace object used"
