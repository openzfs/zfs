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

log_must $FILE_WRITE -o create -f $TESTDIR1/file -b 8192 -c 600 -d 0
log_must $ZFS unmount $TESTDIR1

log_note "Verify that a nested file system with a mounted file system "\
    "cannot be destroyed."
log_mustnot $ZFS destroy $TESTPOOL/$TESTCTR

log_pass "A nested file system was successfully populated."
