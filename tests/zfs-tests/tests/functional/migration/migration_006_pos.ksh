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
# Migrating test file from UFS fs to ZFS fs using cpio
#
# STRATEGY:
# 1. Calculate chksum of testfile
# 2. Cpio up test file and place on a UFS filesystem
# 3. Extract cpio contents to a ZFS file system
# 4. Calculate chksum of extracted file
# 5. Compare old and new chksums.
#

verify_runnable "both"

function cleanup
{
	rm -rf $NONZFS_TESTDIR/cpio$$.cpio $TESTDIR/$BNAME
}

log_assert "Migrating test file from UFS fs to ZFS fs using cpio"

log_onexit cleanup

cwd=$PWD
log_must cd $DNAME
log_must eval "find $BNAME | cpio -oc > $NONZFS_TESTDIR/cpio$$.cpio"
log_must cd $cwd
log_must migrate_cpio $TESTDIR "$NONZFS_TESTDIR/cpio$$.cpio" $SUMA $SUMB

log_pass "Successfully migrated test file from UFS fs to ZFS fs".
