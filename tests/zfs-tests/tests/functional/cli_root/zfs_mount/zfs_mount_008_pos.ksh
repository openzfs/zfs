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
# Copyright (c) 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/cli_root/zfs_mount/zfs_mount.kshlib

#
# DESCRIPTION:
#	'zfs mount -O' allow the file system to be mounted over an existing
#	mount point, making the underlying file system inaccessible.
#
# STRATEGY:
#	1. Create two filesystem fs & fs1, and create two test files for them.
#	2. Unmount fs1 and set mountpoint property is identical to fs.
#	3. Verify 'zfs mount -O' will make the underlying filesystem fs
#	   inaccessible.
#

function cleanup
{
	! ismounted $fs && log_must zfs mount $fs

	datasetexists $fs1 && destroy_dataset $fs1

	if [[ -f $testfile ]]; then
		log_must rm -f $testfile
	fi
}

log_assert "Verify 'zfs mount -O' will override existing mount point."
log_onexit cleanup

fs=$TESTPOOL/$TESTFS; fs1=$TESTPOOL/$TESTFS1

cleanup

# Get the original mountpoint of $fs and $fs1
mntpnt=$(get_prop mountpoint $fs)
log_must zfs create $fs1
mntpnt1=$(get_prop mountpoint $fs1)

testfile=$mntpnt/$TESTFILE0; testfile1=$mntpnt1/$TESTFILE1
log_must mkfile 1M $testfile $testfile1

log_must zfs unmount $fs1
log_must zfs set mountpoint=$mntpnt $fs1
log_must ismounted $fs1
log_must zfs unmount $fs1
log_must zfs mount -O $fs1

# Create new file in override mountpoint
log_must mkfile 1M $mntpnt/$TESTFILE2

# Verify the underlying file system inaccessible
log_mustnot ls $testfile
log_must ls $mntpnt/$TESTFILE1 $mntpnt/$TESTFILE2

# Verify $TESTFILE2 was created in $fs1, rather than $fs
log_must zfs unmount $fs1
log_must zfs set mountpoint=$mntpnt1 $fs1
log_must ismounted $fs1
log_must ls $testfile1 $mntpnt1/$TESTFILE2

# Verify $TESTFILE2 was not created in $fs, and $fs is accessible again.
log_mustnot ls $mntpnt/$TESTFILE2
log_must ls $testfile

log_pass "Verify 'zfs mount -O' override mount point passed."
