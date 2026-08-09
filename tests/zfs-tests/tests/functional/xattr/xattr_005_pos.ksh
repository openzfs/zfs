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
