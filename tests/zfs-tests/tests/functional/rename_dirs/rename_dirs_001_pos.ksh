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

#
# DESCRIPTION:
# Create two directory trees in ZFS filesystem, and concurrently rename
# directory across the two trees. ZFS should be able to handle the race
# situation.
#
# STRATEGY:
# 1. Create a ZFS filesystem
# 2. Make two directory tree in the zfs file system
# 3. Continually rename directory from one tree to another tree in two process
# 4. After the specified time duration, the system should not be panic.
#

verify_runnable "both"

function cleanup
{
	log_must rm -rf $TESTDIR/*
}

log_assert "ZFS can handle race directory rename operation."

log_onexit cleanup

cd $TESTDIR
mkdir -p 1/2/3/4/5 a/b/c/d/e

rename_dir &

sleep 10
if pgrep -x rename_dir >/dev/null 2>&1; then
	pkill -9 -x rename_dir >/dev/null 2>&1
fi

log_pass "ZFS handle race directory rename operation as expected."
