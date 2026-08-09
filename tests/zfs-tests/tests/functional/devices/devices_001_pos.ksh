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
. $STF_SUITE/tests/functional/devices/devices.cfg
. $STF_SUITE/tests/functional/devices/devices_common.kshlib

#
# DESCRIPTION:
# When set property devices=on on file system, devices files can be used in
# this file system.
#
# STRATEGY:
# 1. Create pool and file system.
# 2. Set devices=on on this file system.
# 3. Separately create block device file and character file.
# 4. Separately read and write from those two device files.
# 5. Check the return value, and make sure it succeeds.
#

verify_runnable "global"

log_assert "Setting devices=on on file system, the devices files in this file" \
	"system can be used."
log_onexit cleanup

log_must zfs set devices=on $TESTPOOL/$TESTFS

#
# Create block device file backed by a ZFS volume.
# Verify it can be opened, written, and read.
#
create_dev_file b $TESTDIR/$TESTFILE1 $ZVOL_DEVDIR/$TESTPOOL/$TESTVOL
log_must dd if=/dev/urandom of=$TESTDIR/$TESTFILE1.out1 count=1 bs=128k
log_must dd if=$TESTDIR/$TESTFILE1.out1 of=$TESTDIR/$TESTFILE1 count=1 bs=128k
log_must dd if=$TESTDIR/$TESTFILE1 of=$TESTDIR/$TESTFILE1.out2 count=1 bs=128k
log_must cmp $TESTDIR/$TESTFILE1.out1 $TESTDIR/$TESTFILE1.out2

# Create character device file backed by /dev/null
# Verify it can be opened and written.
create_dev_file c $TESTDIR/$TESTFILE2
log_must dd if=/dev/urandom of=$TESTDIR/$TESTFILE2 count=1 bs=128k

log_pass "Setting devices=on on file system and testing it pass."
