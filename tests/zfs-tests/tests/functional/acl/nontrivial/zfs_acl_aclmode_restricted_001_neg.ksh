#!/usr/bin/ksh -p
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

#
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
# Copyright (c) 2013 by Paul B. Henson <henson@acm.org>.
#                    All rights reserved.
#


. $STF_SUITE/tests/functional/acl/acl.cfg
. $STF_SUITE/tests/functional/acl/acl_common.kshlib

#
# DESCRIPTION:
#	Verify mode bits based chmod fails on files/directories with
#       non-trivial ACLs when aclmode=restricted
#
# STRATEGY:
#	1. Loop super user and non-super user to run the test case
#	2. Create test file and directory
#	3. Set non-trivial ACL on test file and directory
#	4. Verify mode bits based chmod fails
#

verify_runnable "both"

function cleanup
{
	# reset aclmode=discard
	log_must zfs set aclmode=discard $TESTPOOL/$TESTFS
}

log_assert "Verify mode bits based chmod fails on files/directories "\
    "with non-trivial ACLs when aclmode=restricted"
log_onexit cleanup

log_must zfs set aclmode=restricted $TESTPOOL/$TESTFS

for user in root $ZFS_ACL_STAFF1; do
	log_must set_cur_usr $user

	log_must usr_exec mkdir $TESTDIR/testdir
	log_must usr_exec touch $TESTDIR/testfile

	# Make sure ACL is non-trival
	log_must usr_exec chmod A+user:${ZFS_ACL_STAFF1}:r::allow \
	    $TESTDIR/testdir $TESTDIR/testfile

	log_mustnot usr_exec chmod u-w $TESTDIR/testdir
	log_mustnot usr_exec chmod u-w $TESTDIR/testfile

	log_must usr_exec rmdir $TESTDIR/testdir
	log_must usr_exec rm $TESTDIR/testfile
done

log_pass "Verify mode bits based chmod fails on files/directories "\
    "with non-trivial ACLs when aclmode=restricted passed."
