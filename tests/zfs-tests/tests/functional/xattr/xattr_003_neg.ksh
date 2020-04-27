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

function cleanup {

	log_must rm $TESTDIR/myfile.$$

}

log_assert "read/write xattr on a file with no permissions fails"
log_onexit cleanup

log_must touch $TESTDIR/myfile.$$
create_xattr $TESTDIR/myfile.$$ passwd /etc/passwd

log_must chmod 000 $TESTDIR/myfile.$$
if is_illumos; then
	log_mustnot su $ZFS_USER -c "runat $TESTDIR/myfile.$$ cat passwd"
	log_mustnot su $ZFS_USER -c "runat $TESTDIR/myfile.$$ cp /etc/passwd ."
else
	user_run $ZFS_USER eval \
	    "get_xattr passwd $TESTDIR/myfile.$$ >/tmp/passwd.$$"
	log_mustnot diff /etc/passwd /tmp/passwd.$$
	log_must rm /tmp/passwd.$$

	user_run $ZFS_USER eval \
	    "set_xattr_stdin passwd $TESTDIR/myfile.$$ </etc/group"
	log_must chmod 644 $TESTDIR/myfile.$$
	get_xattr passwd $TESTDIR/myfile.$$ >/tmp/passwd.$$
	log_must diff /etc/passwd /tmp/passwd.$$
	log_must rm /tmp/passwd.$$
fi

log_pass "read/write xattr on a file with no permissions fails"
