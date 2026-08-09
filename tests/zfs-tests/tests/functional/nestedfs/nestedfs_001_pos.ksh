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
# Given a pool create a nested file system and a ZFS file system
# in the nested file system. Populate the file system.
#
# As a sub-assertion, the test verifies that a nested file system with
# a mounted file system cannot be destroyed.
#
# STRATEGY:
# 1. Create a file in the new mountpoint
# 2. Unmount the new mountpoint
# 3. Show a nested file system with file systems cannot be destroyed
#

verify_runnable "both"

log_assert "Verify a nested file system can be created/destroyed."

log_must file_write -o create -f $TESTDIR1/file -b 8192 -c 600 -d 0
log_must zfs unmount $TESTDIR1

log_note "Verify that a nested file system with a mounted file system "\
    "cannot be destroyed."
log_mustnot zfs destroy $TESTPOOL/$TESTCTR

log_pass "A nested file system was successfully populated."
