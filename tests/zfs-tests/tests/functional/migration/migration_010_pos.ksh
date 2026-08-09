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
# Migrating test file from ZFS fs to ZFS fs using cp
#
# STRATEGY:
# 1. Calculate chksum of testfile
# 2. CP up test file and place on a ZFS filesystem
# 3. Extract cp contents to a ZFS file system
# 4. Calculate chksum of extracted file
# 5. Compare old and new chksums.
#

verify_runnable "both"

function cleanup
{
	rm -rf $TESTDIR/cp$$.cp $TESTDIR/$BNAME
}

log_assert "Migrating test file from ZFS fs to ZFS fs using cp"

log_onexit cleanup

log_must prepare $DNAME "cp $BNAME $TESTDIR/cp$$.cp"
log_must migrate $TESTDIR $SUMA $SUMB "cp $TESTDIR/cp$$.cp $BNAME"

log_pass "Successfully migrated test file from ZFS fs to ZFS fs".
