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

#
# Copyright (c) 2012 by Delphix. All rights reserved.
#

#
# DESCRIPTION:
#	Verify that user can access file/directory if acltype=posixacl.
#
# STRATEGY:
#	1. Test access to file (mode=rw-)
#	   a. Can modify file
#	   b. Can't create new file
#	   b. Can't execute file
#

verify_runnable "both"
log_assert "Verify acltype=posixacl works on file"

# Test access to FILE
log_note "Testing access to FILE"
log_must touch $TESTDIR/file.0
log_must setfacl -m g:$ZFS_ACL_STAFF_GROUP:rw $TESTDIR/file.0
getfacl $TESTDIR/file.0 2> /dev/null | egrep -q "^group:$ZFS_ACL_STAFF_GROUP:rw-$"
if [ "$?" -eq "0" ]; then
	# Should be able to write to file
	log_must user_run $ZFS_ACL_STAFF1 "echo 'echo test > /dev/null' > $TESTDIR/file.0"

	# Should NOT be able to create new file
	log_mustnot user_run $ZFS_ACL_STAFF1 "touch $TESTDIR/file.1"

	# Root should be able to run file, but not user
	chmod +x $TESTDIR/file.0
	log_must $TESTDIR/file.0
	log_mustnot user_run $ZFS_ACL_STAFF1 $TESTDIR/file.0

	log_pass "POSIX ACL mode works on files"
else
	log_fail "Group '$ZFS_ACL_STAFF_GROUP' does not have 'rw' as specified"
fi
