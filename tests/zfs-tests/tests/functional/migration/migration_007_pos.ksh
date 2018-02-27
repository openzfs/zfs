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
# Migrating test file from ZFS fs to ZFS fs using dd.
#
# STRATEGY:
# 1. Calculate chksum of testfile
# 2. Dd up test file and place on a ZFS filesystem
# 3. Extract dd contents to a ZFS file system
# 4. Calculate chksum of extracted file
# 5. Compare old and new chksums.
#

verify_runnable "both"

function cleanup
{
	rm -rf $TESTDIR/dd$$.dd
	rm -rf $TESTDIR/$BNAME
}

log_assert "Migrating test file from ZFS fs to ZFS fs using dd"

log_onexit cleanup

prepare $DNAME "dd if=$BNAME obs=128k of=$TESTDIR/dd$$.dd"
(( $? != 0 )) && log_fail "Unable to create src archive"

migrate $TESTDIR $SUMA $SUMB "dd if=$TESTDIR/dd$$.dd obs=128k of=$BNAME"
(( $? != 0 )) && log_fail "Unable to successfully migrate test file from" \
    "ZFS fs to ZFS fs"

log_pass "Successfully migrated test file from ZFS fs to ZFS fs".
