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
#	Project ID affects POSIX behavior
#
#
# STRATEGY:
#	1. Create three directories
#	2. Set tdir1 and tdir3 project ID as PRJID1,
#	   set tdir2 project ID as PRJID2.
#	3. Create regular file under tdir1. It inherits tdir1 project ID.
#	4. Hardlink from tdir1's child to tdir2 should be denied,
#	   move tdir1's child to tdir2 will be object recreated.
#	5. Hardlink from tdir1's child to tdir3 should succeed.
#

function cleanup
{
	log_must rm -rf $PRJDIR1
	log_must rm -rf $PRJDIR2
	log_must rm -rf $PRJDIR3
}

if ! lsattr -pd > /dev/null 2>&1; then
	log_unsupported "Current e2fsprogs does not support set/show project ID"
fi

log_onexit cleanup

log_assert "Project ID affects POSIX behavior"

log_must mkdir $PRJDIR1
log_must mkdir $PRJDIR2
log_must mkdir $PRJDIR3
log_must mkdir $PRJDIR3/dir

log_must chattr +P -p $PRJID1 $PRJDIR1
log_must chattr +P -p $PRJID2 $PRJDIR2

log_must touch $PRJDIR1/tfile1
log_must touch $PRJDIR1/tfile2
log_must eval "lsattr -p $PRJDIR1/tfile1 | grep $PRJID1"

log_mustnot ln $PRJDIR1/tfile1 $PRJDIR2/tfile2

log_must mv $PRJDIR1/tfile1 $PRJDIR2/tfile2
log_must eval "lsattr -p $PRJDIR2/tfile2 | grep $PRJID2"

log_must mv $PRJDIR3/dir $PRJDIR2/
log_must eval "lsattr -dp $PRJDIR2/dir | grep $PRJID2"

log_must chattr +P -p $PRJID1 $PRJDIR3
log_must ln $PRJDIR1/tfile2 $PRJDIR3/tfile3

log_pass "Project ID affects POSIX behavior"
