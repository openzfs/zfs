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
#	'zfs recv -F' to force rollback.
#
# STRATEGY:
#	1. Create pool and fs.
#	2. Create some files in fs and take a snapshot1.
#	3. Create another files in fs and take snapshot2.
#	4. Create incremental stream from snapshot1 to snapshot2.
#	5. fs rollback to snapshot1 and modify fs.
#	6. Verify 'zfs recv -F' can force rollback.
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

log_assert "'zfs recv -F' to force rollback."
log_onexit cleanup

ibackup=$TEST_BASE_DIR/ibackup.$$
fs=$TESTPOOL/$TESTFS; snap1=$fs@snap1; snap2=$fs@snap2

mntpnt=$(get_prop mountpoint $fs)
log_must mkfile 10m $mntpnt/file1
log_must zfs snapshot $snap1
log_must mkfile 10m $mntpnt/file2
log_must zfs snapshot $snap2

log_must eval "zfs send -i $snap1 $snap2 > $ibackup"

log_note "Verify 'zfs receive' succeed, if filesystem was not modified."
log_must zfs rollback -r $snap1
log_must eval "zfs receive $fs < $ibackup"
if [[ ! -f $mntpnt/file1 || ! -f $mntpnt/file2 ]]; then
	log_fail "'zfs receive' failed."
fi

log_note "Verify 'zfs receive' failed if filesystem was modified."
log_must zfs rollback -r $snap1
log_must rm -rf $mntpnt/file1
log_mustnot eval "zfs receive $fs < $ibackup"

# Verify 'zfs receive -F' to force rollback whatever filesystem was modified.
log_must zfs rollback -r $snap1
log_must rm -rf $mntpnt/file1
log_must eval "zfs receive -F $fs < $ibackup"
if [[ ! -f $mntpnt/file1 || ! -f $mntpnt/file2 ]]; then
	log_fail "'zfs receive -F' failed."
fi

log_pass "'zfs recv -F' to force rollback passed."
