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
