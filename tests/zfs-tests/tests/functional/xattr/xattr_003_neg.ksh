#!/bin/ksh -p
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or https://opensource.org/licenses/CDDL-1.0.
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
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/xattr/xattr_common.kshlib

#
# DESCRIPTION:
#
# Attempting to read an xattr on a file for which we have no permissions
# should fail.
#
# STRATEGY:
#	1. Create a file with an xattr
#	2. Set the file permissions to 000
#	3. Check that we're unable to read the xattr as a non-root user
#	4. Check that we're unable to write an xattr as a non-root user
#

function cleanup
{
	rm -f $testfile $tempfile
}

log_assert "read/write xattr on a file with no permissions fails"
log_onexit cleanup

typeset testfile=$TESTDIR/testfile.$$
typeset tempfile=/tmp/tempfile.$$

log_must touch $testfile
create_xattr $testfile passwd /etc/passwd

log_must chmod 000 $testfile
if is_illumos; then
	log_mustnot user_run $ZFS_USER runat $testfile cat passwd
	log_mustnot user_run $ZFS_USER runat $testfile cp /etc/passwd .
else
	log_mustnot user_run $ZFS_USER "
. $STF_SUITE/include/libtest.shlib
get_xattr passwd $testfile >$tempfile
"
	log_mustnot diff -q /etc/passwd $tempfile
	log_must rm $tempfile

	log_mustnot user_run $ZFS_USER "
. $STF_SUITE/include/libtest.shlib
set_xattr_stdin passwd $testfile </etc/group
"
	log_must chmod 644 $testfile
	get_xattr passwd $testfile >$tempfile
	log_must diff -q /etc/passwd $tempfile
	log_must rm $tempfile
fi

log_pass "read/write xattr on a file with no permissions fails"
