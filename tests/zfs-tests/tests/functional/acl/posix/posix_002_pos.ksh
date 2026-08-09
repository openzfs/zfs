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
if ! ls -l $TESTDIR | grep "dir.0" | grep -q "drwx-wx---+"; then
	msk=$(ls -l $TESTDIR | awk '/dir.0/ {print $1}')
	log_note "expected mask drwx-wx---+ but found $msk"
	log_fail "Expected permissions were not set."
fi
if getfacl $TESTDIR/dir.0 2> /dev/null |
    grep -q "^group:$ZFS_ACL_STAFF_GROUP:-wx$"
then
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
