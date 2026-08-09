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
#	1. Test access to file (mode=rw-)
#	   a. Can modify file
#	   b. Can't create new file
#	   b. Can't execute file
#

verify_runnable "both"

function cleanup
{
	rmdir $TESTDIR/dir.0
}

log_assert "Verify acltype=posix works on file"
log_onexit cleanup

# Test access to FILE
log_note "Testing access to FILE"
log_must touch $TESTDIR/file.0
log_must setfacl -m g:$ZFS_ACL_STAFF_GROUP:rw $TESTDIR/file.0
if ! getfacl $TESTDIR/file.0 2> /dev/null |
    grep -qFx "group:$ZFS_ACL_STAFF_GROUP:rw-"
then
	log_note "$(getfacl $TESTDIR/file.0 2> /dev/null)"
	log_fail "Group '$ZFS_ACL_STAFF_GROUP' does not have 'rw' as specified"
fi

# Should be able to write to file
log_must user_run $ZFS_ACL_STAFF1 \
    "echo 'echo test > /dev/null' > $TESTDIR/file.0"

# Since $TESTDIR is 777, create a new dir with controlled permissions
# for testing that creating a new file is not allowed.
log_must mkdir $TESTDIR/dir.0
log_must chmod 700 $TESTDIR/dir.0
log_must setfacl -m g:$ZFS_ACL_STAFF_GROUP:rw $TESTDIR/dir.0
# Confirm permissions
msk=$(ls -ld $TESTDIR/dir.0 | awk '{print $1}')
if ! [ "$msk" = "drwxrw----+" ]; then
	log_note "expected mask drwxrw----+ but found $msk"
	log_fail "Expected permissions were not set."
fi

if ! getfacl $TESTDIR/dir.0 2> /dev/null |
    grep -qFx "group:$ZFS_ACL_STAFF_GROUP:rw-"
then
	log_note "$(getfacl $TESTDIR/dir.0 2> /dev/null)"
	log_fail "ACL group:$ZFS_ACL_STAFF_GROUP:rw- was not set."
fi
# Should NOT be able to create new file
log_mustnot user_run $ZFS_ACL_STAFF1 "touch $TESTDIR/dir.0/file.1"

# Root should be able to run file, but not user
chmod +x $TESTDIR/file.0
log_must $TESTDIR/file.0
log_mustnot user_run $ZFS_ACL_STAFF1 $TESTDIR/file.0

log_pass "POSIX ACL mode works on files"
