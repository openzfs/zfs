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
# The noxattr mount option functions as expected
#
# STRATEGY:
#	1. Create a file on a filesystem and add an xattr to it
#	2. Unmount the filesystem, and mount it -o noxattr
#	3. Verify that the xattr cannot be read and new files
#	   cannot have xattrs set on them.
#	4. Unmount and mount the filesystem normally
#	5. Verify that xattrs can be set and accessed again
#

function cleanup {

	log_must rm $TESTDIR/myfile.$$
}


log_assert "The noxattr mount option functions as expected"
log_onexit cleanup

log_must touch $TESTDIR/myfile.$$
create_xattr $TESTDIR/myfile.$$ passwd /etc/passwd

log_must umount $TESTDIR
log_must zfs mount -o noxattr $TESTPOOL/$TESTFS

# check that we can't perform xattr operations
if is_illumos; then
	log_mustnot eval "runat $TESTDIR/myfile.$$ cat passwd > /dev/null 2>&1"
	log_mustnot eval "runat $TESTDIR/myfile.$$ rm passwd > /dev/null 2>&1"
	log_mustnot eval "runat $TESTDIR/myfile.$$ cp /etc/passwd . \
	    > /dev/null 2>&1"

	log_must touch $TESTDIR/new.$$
	log_mustnot eval "runat $TESTDIR/new.$$ cp /etc/passwd . \
	    > /dev/null 2>&1"
	log_mustnot eval "runat $TESTDIR/new.$$ rm passwd > /dev/null 2>&1"
else
	log_mustnot get_xattr passwd $TESTDIR/myfile.$$
	log_mustnot rm_xattr passwd $TESTDIR/myfile.$$
	log_mustnot set_xattr_stdin passwd $TESTDIR/myfile.$$ </etc/passwd

	log_must touch $TESTDIR/new.$$
	log_mustnot set_xattr_stdin passwd $TESTDIR/new.$$ </etc/passwd
	log_mustnot rm_xattr passwd $TESTDIR/new.$$
fi

# now mount the filesystem again as normal
log_must umount $TESTDIR
log_must zfs mount $TESTPOOL/$TESTFS

# we should still have an xattr on the first file
verify_xattr $TESTDIR/myfile.$$ passwd /etc/passwd

# there should be no xattr on the file we created while the fs was mounted
# -o noxattr
if is_illumos; then
	log_mustnot eval "runat $TESTDIR/new.$$ cat passwd > /dev/null 2>&1"
else
	log_mustnot get_xattr passwd $TESTDIR/new.$$
fi
create_xattr $TESTDIR/new.$$ passwd /etc/passwd

log_pass "The noxattr mount option functions as expected"
