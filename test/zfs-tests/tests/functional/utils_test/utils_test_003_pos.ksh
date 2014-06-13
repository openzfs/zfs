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
# Copyright (c) 2013 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/utils_test/utils_test.kshlib

#
# DESCRIPTION:
# Ensure that the fsdb(1M) utility fails on a ZFS file system.
#
# STRATEGY:
# 1. Populate a ZFS directory with a number of files.
# 2. Run fsdb against the raw device.
# 3. Ensure it fails.
#

verify_runnable "global"

function cleanup
{
	$RM -rf $TESTDIR/*
}

log_onexit cleanup

log_assert "Ensure that the fsdb(1M) utility fails on a ZFS file system."

populate_dir $NUM_FILES
inode_num=`$LS -li $TESTDIR/$TESTFILE.0 | $AWK '{print $1}'`
[[ -z $inode_num ]] && \
    log_fail "Failed to determine inode of file: $TESTDIR/$TESTFILE.0"

log_mustnot $ECHO ":inode $inode_num" | $FSDB /dev/rdsk/${DISK}s0

log_pass "fsdb(1M) returned an error as expected."
