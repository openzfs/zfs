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
# Xattrs present on a file in a snapshot should be visible.
#
# STRATEGY:
#	1. Create a file and give it an xattr
#       2. Take a snapshot of the filesystem
#	3. Verify that we can take a snapshot of it.
#

function cleanup {

	log_must zfs destroy $TESTPOOL/$TESTFS@snap
	log_must rm $TESTDIR/myfile.$$

}

log_assert "read xattr on a snapshot"
log_onexit cleanup

# create a file, and an xattr on it
log_must touch $TESTDIR/myfile.$$
create_xattr $TESTDIR/myfile.$$ passwd /etc/passwd

# snapshot the filesystem
log_must zfs snapshot $TESTPOOL/$TESTFS@snap

# check for the xattr on the snapshot
verify_xattr $TESTDIR/.zfs/snapshot/snap/myfile.$$ passwd /etc/passwd

log_pass "read xattr on a snapshot"
