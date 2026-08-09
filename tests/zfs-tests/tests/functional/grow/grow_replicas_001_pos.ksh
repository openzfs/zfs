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
# Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
# Copyright 2016 Nexenta Systems, Inc.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/grow/grow.cfg

# DESCRIPTION:
# A ZFS filesystem is limited by the amount of disk space
# available to the pool. Growing the pool by adding a disk
# increases the amount of space.
#
# STRATEGY:
# 1. Fill the filesystem on mirror/raidz pool by writing a file until ENOSPC.
# 2. Grow the mirror/raidz pool by adding another mirror/raidz vdev.
# 3. Verify that more data can now be written to the filesystem.

verify_runnable "global"

function cleanup
{
	destroy_pool $TESTPOOL
	rm -f $DEVICE1 $DEVICE2 $DEVICE3 $DEVICE4
}

log_assert "mirror/raidz pool may be increased in capacity by adding a disk"

log_onexit cleanup

readonly ENOSPC=28

for pooltype in "mirror" "raidz"; do
	log_note "Creating pool type: $pooltype"

	truncate -s $SPA_MINDEVSIZE $DEVICE1 $DEVICE2
	create_pool $TESTPOOL $pooltype $DEVICE1 $DEVICE2

	log_must zfs create $TESTPOOL/$TESTFS
	log_must zfs set mountpoint=$TESTDIR $TESTPOOL/$TESTFS
	log_must zfs set compression=off $TESTPOOL/$TESTFS

	file_write -o create -f $TESTDIR/$TESTFILE1 \
            -b $BLOCK_SIZE -c $WRITE_COUNT -d 0

	[[ $? -ne $ENOSPC ]] && \
	    log_fail "file_write completed w/o ENOSPC"

	[[ ! -s $TESTDIR/$TESTFILE1 ]] && \
	    log_fail "$TESTDIR/$TESTFILE1 was not created"

	truncate -s $SPA_MINDEVSIZE $DEVICE3 $DEVICE4
	log_must zpool add $TESTPOOL $pooltype $DEVICE3 $DEVICE4

	log_must file_write -o append -f $TESTDIR/$TESTFILE1 \
	    -b $BLOCK_SIZE -c $SMALL_WRITE_COUNT -d 0

	log_must destroy_pool $TESTPOOL
	rm -f $DEVICE1 $DEVICE2 $DEVICE3 $DEVICE4
done

log_pass "mirror/raidz pool successfully grown"
