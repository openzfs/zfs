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
# Copyright (c) 2013 by Delphix. All rights reserved.
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

	log_must $RM $TESTDIR/myfile.$$
}


log_assert "The noxattr mount option functions as expected"
log_onexit cleanup

$ZFS set 2>&1 | $GREP xattr > /dev/null
if [ $? -ne 0 ]
then
	log_unsupported "noxattr mount option not supported on this release."
fi

log_must $TOUCH $TESTDIR/myfile.$$
create_xattr $TESTDIR/myfile.$$ passwd /etc/passwd

log_must $UMOUNT $TESTDIR
log_must $ZFS mount -o noxattr $TESTPOOL/$TESTFS

# check that we can't perform xattr operations
log_mustnot eval "$RUNAT $TESTDIR/myfile.$$ $CAT passwd > /dev/null 2>&1"
log_mustnot eval "$RUNAT $TESTDIR/myfile.$$ $RM passwd > /dev/null 2>&1"
log_mustnot eval "$RUNAT $TESTDIR/myfile.$$ $CP /etc/passwd . > /dev/null 2>&1"

log_must $TOUCH $TESTDIR/new.$$
log_mustnot eval "$RUNAT $TESTDIR/new.$$ $CP /etc/passwd . > /dev/null 2>&1"
log_mustnot eval "$RUNAT $TESTDIR/new.$$ $RM passwd > /dev/null 2>&1"

# now mount the filesystem again as normal
log_must $UMOUNT $TESTDIR
log_must $ZFS mount $TESTPOOL/$TESTFS

# we should still have an xattr on the first file
verify_xattr $TESTDIR/myfile.$$ passwd /etc/passwd

# there should be no xattr on the file we created while the fs was mounted
# -o noxattr
log_mustnot eval "$RUNAT $TESTDIR/new.$$ $CAT passwd > /dev/null 2>&1"
create_xattr $TESTDIR/new.$$ passwd /etc/passwd

log_pass "The noxattr mount option functions as expected"
