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
#	Check the basic function project{obj}used
#
#
# STRATEGY:
#	1. Write data to fs with some project then check the project{obj}used
#

function cleanup
{
	cleanup_projectquota
}

if ! lsattr -pd > /dev/null 2>&1; then
	log_unsupported "Current e2fsprogs does not support set/show project ID"
fi

log_onexit cleanup

log_assert "Check the basic function of project{obj}used"

sync_pool
typeset project_used=$(get_prop "projectused@$PRJID1" $QFS)
typeset file_size='10m'

if [[ $project_used != 0 ]]; then
	log_fail "FAIL: projectused is $project_used, should be 0"
fi

mkmount_writable $QFS
log_must user_run $PUSER mkdir $PRJDIR
log_must chattr +P -p $PRJID1 $PRJDIR
log_must user_run $PUSER mkfile $file_size $PRJDIR/qf
sync_pool
project_used=$(get_prop "projectused@$PRJID1" $QFS)
# get_prop() reads the exact byte value which is slightly more than 10m
if [[ "$(($project_used/1024/1024))m" != "$file_size" ]]; then
	log_note "project $PRJID1 used is $project_used"
	log_fail "projectused for project $PRJID1 expected to be $file_size, " \
	    "not $project_used"
fi

log_must rm -rf $PRJDIR
typeset project_obj_used=$(get_prop "projectobjused@$PRJID2" $QFS)
typeset file_count=100

if [[ $project_obj_used != 0 ]]; then
	log_fail "FAIL: projectobjused is $project_obj_used, should be 0"
fi

log_must zfs set xattr=sa $QFS
log_must user_run $PUSER mkdir $PRJDIR
log_must chattr +P -p $PRJID2 $PRJDIR
# $PRJDIR has already used one object with the $PRJID2
log_must user_run $PUSER mkfiles $PRJDIR/qf_ $((file_count - 1))
sync_pool
project_obj_used=$(get_prop "projectobjused@$PRJID2" $QFS)
if [[ $project_obj_used != $file_count ]]; then
	log_note "project $PRJID2 used is $project_obj_used"
	log_fail "projectobjused for project $PRJID2 expected to be " \
	    "$file_count, not $project_obj_used"
fi

log_pass "Check the basic function of project{obj}used pass as expect"
