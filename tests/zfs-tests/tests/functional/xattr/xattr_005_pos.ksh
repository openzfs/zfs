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
# read/write/create/delete xattr on a clone filesystem
#
#
# STRATEGY:
#	1. Create an xattr on a filesystem
#	2. Snapshot the filesystem and clone it
#       3. Verify the xattr can still be read, written, deleted
#	4. Verify we can create new xattrs on new files created on the clone
#

function cleanup {

	log_must zfs destroy $TESTPOOL/$TESTFS/clone
	log_must zfs destroy $TESTPOOL/$TESTFS@snapshot1
	log_must rm $TESTDIR/myfile.$$
}

log_assert "read/write/create/delete xattr on a clone filesystem"
log_onexit cleanup

# create a file, and an xattr on it
log_must touch $TESTDIR/myfile.$$
create_xattr $TESTDIR/myfile.$$ passwd /etc/passwd

# snapshot & clone the filesystem
log_must zfs snapshot $TESTPOOL/$TESTFS@snapshot1
log_must zfs clone $TESTPOOL/$TESTFS@snapshot1 $TESTPOOL/$TESTFS/clone
log_must zfs set mountpoint=$TESTDIR/clone $TESTPOOL/$TESTFS/clone

# check for the xattrs on the clone
verify_xattr $TESTDIR/clone/myfile.$$ passwd /etc/passwd

# check we can create xattrs on the clone
create_xattr $TESTDIR/clone/myfile.$$ foo /etc/passwd
delete_xattr $TESTDIR/clone/myfile.$$ foo

# delete the original dataset xattr
delete_xattr $TESTDIR/myfile.$$ passwd

# verify it's still there on the clone
verify_xattr $TESTDIR/clone/myfile.$$ passwd /etc/passwd
delete_xattr $TESTDIR/clone/myfile.$$ passwd

log_pass "read/write/create/delete xattr on a clone filesystem"
