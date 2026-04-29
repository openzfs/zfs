#!/bin/ksh
# SPDX-License-Identifier: CDDL-1.0

#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# http://www.illumos.org/license/CDDL.
#

#
# Copyright (c) 2019 by Datto, Inc. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# Description:
# Ensure zdb -f only works on raidz
#
# Strategery:
# 1. Create a pool with one disk
# 2. Create a file
# 3. Get the inode number of the file
# 4. Run zdb -f
# 5. Confirm failure status

function cleanup
{
        destroy_pool $TESTPOOL1
	rm -f $TESTDIR/file1.bin
}

log_assert "Verify zdb -f fails on non-raidz pool"
log_onexit cleanup

# 1. Create a RAIDZ1 pool
log_must mkdir -p $TESTDIR
touch $TESTDIR/file1.bin
log_must truncate -s 128m $TESTDIR/file1.bin
log_must zpool create -f $TESTPOOL1 $TESTDIR/file1.bin

# 2. Create a file
log_must touch /$TESTPOOL1/file.txt

# 3. Get the inode number of the file
INUM=$(ls -li /$TESTDIR/file1.txt | cut -f1 -d ' ')

# 4. Run zdb -f
log_mustnot zdb -f $TESTDIR/ $INUM

log_pass "'zdb -f' fails on non-raidz as expected."
