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

#
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/migration/migration.kshlib

#
# DESCRIPTION:
# Migrating test file from platform native fs to ZFS fs using tar.
#
# STRATEGY:
# 1. Calculate chksum of testfile
# 2. Tar up test file and place on a platform native filesystem
# 3. Extract tar contents to a ZFS file system
# 4. Calculate chksum of extracted file
# 5. Compare old and new chksums.
#

verify_runnable "both"

function cleanup
{
	rm -rf $NONZFS_TESTDIR/tar$$.tar $TESTDIR/$BNAME
}

log_assert "Migrating test file from $NEWFS_DEFAULT_FS fs to ZFS fs using tar"

log_onexit cleanup

log_must prepare $DNAME "tar cf $NONZFS_TESTDIR/tar$$.tar $BNAME"
log_must migrate $TESTDIR $SUMA $SUMB "tar xvf $NONZFS_TESTDIR/tar$$.tar"

log_pass "Successfully migrated test file from $NEWFS_DEFAULT_FS fs to ZFS fs".
