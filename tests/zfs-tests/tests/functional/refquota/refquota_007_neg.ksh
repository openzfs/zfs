#!/bin/ksh
#
# CDDL HEADER START
#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.

# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# http://www.illumos.org/license/CDDL.
#
# CDDL HEADER END
#

#
# Copyright (c) 2013 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#       refquota limits the amount of space a dataset can consume,
#       snapshot rollback should be limited by refquota.
#
# STRATEGY:
#       1. Create a file in a filesystem
#       2. Create a snapshot of the filesystem
#       3. Remove the file
#       4. Set a refquota of size half of the file
#       5. Rollback the filesystem from the snapshot
#       6. Rollback should fail
#

verify_runnable "both"

function cleanup
{
	log_must zfs destroy -rf $TESTPOOL/$TESTFS
	log_must zfs create $TESTPOOL/$TESTFS
	log_must zfs set mountpoint=$TESTDIR $TESTPOOL/$TESTFS
}

log_onexit cleanup

TESTFILE='testfile'
FS=$TESTPOOL/$TESTFS

mntpnt=$(get_prop mountpoint $FS)
log_must mkfile 20M $mntpnt/$TESTFILE
log_must zfs snapshot $FS@snap20M
log_must rm $mntpnt/$TESTFILE

sync_all_pools

log_must zfs set refquota=10M $FS
log_mustnot zfs rollback $FS@snap20M

log_pass "The rollback to the snapshot was restricted by refquota."
