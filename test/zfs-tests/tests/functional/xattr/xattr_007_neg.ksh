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
# Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2013 by Delphix. All rights reserved.
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
	log_must $ZFS destroy $TESTPOOL/$TESTFS@snap
	log_must $RM $TESTDIR/myfile2.$$
	log_must $RM $TESTDIR/myfile.$$
	log_must $RM /tmp/output.$$
	[[ -e /tmp/expected_output.$$ ]]  && log_must $RM  \
	/tmp/expected_output.$$

}

log_assert "create/write xattr on a snapshot fails"
log_onexit cleanup

# create a file, and an xattr on it
log_must $TOUCH $TESTDIR/myfile.$$
create_xattr $TESTDIR/myfile.$$ passwd /etc/passwd

# create another file that doesn't have an xattr
log_must $TOUCH $TESTDIR/myfile2.$$

# snapshot the filesystem
log_must $ZFS snapshot $TESTPOOL/$TESTFS@snap

# we shouldn't be able to alter the first file's xattr
log_mustnot eval " $RUNAT $TESTDIR/.zfs/snapshot/snap/myfile.$$ \
    $CP /etc/passwd .  >/tmp/output.$$  2>&1"
log_must $GREP  -i  Read-only  /tmp/output.$$

log_must eval "$RUNAT $TESTDIR/.zfs/snapshot/snap/myfile2.$$  \
    $LS >/tmp/output.$$  2>&1"
create_expected_output  /tmp/expected_output.$$ SUNWattr_ro SUNWattr_rw
log_must $DIFF /tmp/output.$$ /tmp/expected_output.$$

log_pass "create/write xattr on a snapshot fails"
