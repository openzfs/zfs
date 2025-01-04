#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
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
# Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/xattr/xattr_common.kshlib

#
# DESCRIPTION:
# Creating and writing xattrs on files in snapshot directories fails. Also,
# we shouldn't be able to list the xattrs of files in snapshots who didn't have
# xattrs when the snapshot was created (the xattr namespace wouldn't have been
# created yet, and snapshots are read-only) See fsattr(5) for more details.
#
# STRATEGY:
#	1. Create a file and add an xattr to it.
#	2. Create another file, but don't add an xattr to it.
#       3. Snapshot the filesystem
#	4. Verify we're unable to alter the xattr on the first file
#	5. Verify we're unable to list the xattrs on the second file
#

function cleanup {
	log_must zfs destroy $TESTPOOL/$TESTFS@snap
	log_must rm $TESTDIR/myfile2.$$
	log_must rm $TESTDIR/myfile.$$
	log_must rm $TEST_BASE_DIR/output.$$
	[[ -e $TEST_BASE_DIR/expected_output.$$ ]]  && log_must rm  \
	$TEST_BASE_DIR/expected_output.$$
}

log_assert "create/write xattr on a snapshot fails"
log_onexit cleanup

# create a file, and an xattr on it
log_must touch $TESTDIR/myfile.$$
create_xattr $TESTDIR/myfile.$$ passwd /etc/passwd

# create another file that doesn't have an xattr
log_must touch $TESTDIR/myfile2.$$

# snapshot the filesystem
log_must zfs snapshot $TESTPOOL/$TESTFS@snap

# we shouldn't be able to alter the first file's xattr
if is_illumos; then
	log_mustnot eval " runat $TESTDIR/.zfs/snapshot/snap/myfile.$$ \
	    cp /etc/passwd .  > $TEST_BASE_DIR/output.$$  2>&1"
	log_must grep  -i  Read-only  $TEST_BASE_DIR/output.$$
	log_must eval "runat $TESTDIR/.zfs/snapshot/snap/myfile2.$$  \
	    ls > $TEST_BASE_DIR/output.$$  2>&1"
	create_expected_output  $TEST_BASE_DIR/expected_output.$$ SUNWattr_ro SUNWattr_rw
else
	log_mustnot eval "set_xattr_stdin cp $TESTDIR/.zfs/snapshot/snap/myfile.$$ \
	     </etc/passwd  > $TEST_BASE_DIR/output.$$  2>&1"
	log_must grep  -i  Read-only  $TEST_BASE_DIR/output.$$
	log_must eval "ls_xattr $TESTDIR/.zfs/snapshot/snap/myfile2.$$ \
	    > $TEST_BASE_DIR/output.$$  2>&1"
	log_must eval "ls_xattr $TESTDIR/myfile2.$$ > $TEST_BASE_DIR/expected_output.$$"
fi

log_must diff $TEST_BASE_DIR/output.$$ $TEST_BASE_DIR/expected_output.$$

log_pass "create/write xattr on a snapshot fails"
