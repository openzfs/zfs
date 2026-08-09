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
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/mmap/mmap.cfg

#
# DESCRIPTION:
# read()s from mmap()'ed file contain correct data.
#
# STRATEGY:
# 1. Create a pool & dataset
# 2. Call readmmap binary
# 3. unmount this file system
# 4. Verify the integrity of this pool & dataset
#

verify_runnable "global"

log_assert "read()s from mmap()'ed file contain correct data."

log_must chmod 777 $TESTDIR
log_must readmmap $TESTDIR/test-read-file
log_must zfs unmount $TESTPOOL/$TESTFS

typeset dir=$(get_device_dir $DISKS)
verify_filesys "$TESTPOOL" "$TESTPOOL/$TESTFS" "$dir"

log_pass "read(2) calls from a mmap(2)'ed file succeeded."
