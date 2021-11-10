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
# Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/acl/acl_common.kshlib

#
# Copyright (c) 2012 by Delphix. All rights reserved.
#

#
# DESCRIPTION:
#	Verify that user can access file/directory if acltype=posix.
#
# STRATEGY:
#	1. Test access to directory (mode=-wx)
#	   a. Can create file in dir
#	   b. Can't list directory
#

verify_runnable "both"
log_assert "Verify acltype=posix works on directory"

# Test access to DIRECTORY
log_note "Testing access to DIRECTORY"
log_must mkdir $TESTDIR/dir.0
# Eliminate access by "other" including our test group,
# we want access controlled only by the ACLs.
log_must chmod 700 $TESTDIR/dir.0
log_must setfacl -m g:$ZFS_ACL_STAFF_GROUP:wx $TESTDIR/dir.0
# Confirm permissions
ls -l $TESTDIR |grep "dir.0" |grep -q "drwx-wx---+"
if [ "$?" -ne "0" ]; then
	msk=$(ls -l $TESTDIR |grep "dir.0" | awk '{print $1}')
	log_note "expected mask drwx-wx---+ but found $msk"
	log_fail "Expected permissions were not set."
fi
getfacl $TESTDIR/dir.0 2> /dev/null | egrep -q \
    "^group:$ZFS_ACL_STAFF_GROUP:-wx$"
if [ "$?" -eq "0" ]; then
	# Should be able to create file in directory
	log_must user_run $ZFS_ACL_STAFF1 "touch $TESTDIR/dir.0/file.0"

	# Should NOT be able to list files in directory
	log_mustnot user_run $ZFS_ACL_STAFF1 "ls -l $TESTDIR/dir.0"

	log_pass "POSIX ACL mode works on directories"
else
	acl=$(getfacl $TESTDIR/dir.0 2> /dev/null)
	log_note $acl
	log_fail "Group '$ZFS_ACL_STAFF_GROUP' does not have '-wx' as specified"
fi
