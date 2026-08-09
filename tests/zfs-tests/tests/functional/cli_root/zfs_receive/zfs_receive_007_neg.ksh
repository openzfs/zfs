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
# Copyright (c) 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#	'zfs recv -F' should fail if the incremental stream does not match
#
# STRATEGY:
#	1. Create pool and fs.
#	2. Create some files in fs and take snapshots.
#	3. Keep the incremental stream and restore the stream to the pool
#	4. Verify receiving the stream fails
#

verify_runnable "both"

function cleanup
{
	for snap in $snap2 $snap1; do
		datasetexists $snap && destroy_dataset $snap -rf
	done
	for file in $ibackup $mntpnt/file1 $mntpnt/file2; do
		[[ -f $file ]] && log_must rm -f $file
	done
}

log_assert "'zfs recv -F' should fail if the incremental stream does not match"
log_onexit cleanup

fs=$TESTPOOL/$TESTFS
snap1=$fs@snap1
snap2=$fs@snap2
ibackup=$TEST_BASE_DIR/ibackup.$$

datasetexists $fs || log_must zfs create $fs

mntpnt=$(get_prop mountpoint $fs)
log_must mkfile 10m $mntpnt/file1
log_must zfs snapshot $snap1
log_must mkfile 10m $mntpnt/file2
log_must zfs snapshot $snap2

log_must eval "zfs send -i $snap1 $snap2 > $ibackup"

log_must zfs destroy $snap1
log_must zfs destroy $snap2
log_mustnot eval "zfs receive -F $fs < $ibackup"

log_must mkfile 20m $mntpnt/file1
log_must rm -rf $mntpnt/file2
log_must zfs snapshot $snap1
log_mustnot eval "zfs receive -F $snap2 < $ibackup"

log_pass "'zfs recv -F' should fail if the incremental stream does not match"
