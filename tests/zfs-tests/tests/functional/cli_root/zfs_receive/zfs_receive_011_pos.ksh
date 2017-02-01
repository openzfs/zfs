#!/bin/ksh -p
#
# CDDL HEADER START
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
# CDDL HEADER END
#

#
# Copyright 2015, OmniTI Computer Consulting, Inc. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#	refquota should be sent-and-received, but it should not interfere with
#	the receipt of intermediate snapshots that may have preceded the
#	final snapshot, where the refquota should hold.
#
# STRATEGY:
#	1. Create a filesystem.
#	2. Create two equal-sized large files.
#	3. Snapshot the filesystem.
#	4. Remove one of the two large files.
#	5. Create a refquota larger than one file, but smaller than both.
#	6. Snapshot the filesystem again.
#	7. Send a replication stream of the second snapshot to a new filesystem.
#
#

verify_runnable "both"

typeset streamfile=/var/tmp/streamfile.$$

function cleanup
{
	log_must $RM $streamfile
	log_must $ZFS destroy -rf $TESTPOOL/$TESTFS1
	log_must $ZFS destroy -rf $TESTPOOL/$TESTFS2
}

log_assert "refquota is properly sent-and-received, regardless of any " \
	"intermediate snapshots sent by a replication stream."
log_onexit cleanup

orig=$TESTPOOL/$TESTFS1
dest=$TESTPOOL/$TESTFS2
#	1. Create a filesystem.
log_must $ZFS create $orig
origdir=$(get_prop mountpoint $orig)

#	2. Create two equal-sized large files.
log_must $MKFILE 5M $origdir/file1
log_must $MKFILE 5M $origdir/file2
log_must $SYNC

#	3. Snapshot the filesystem.
log_must $ZFS snapshot $orig@1

#	4. Remove one of the two large files.
log_must $RM $origdir/file2
log_must $SYNC

#	5. Create a refquota larger than one file, but smaller than both.
log_must $ZFS set refquota=8M $orig

#	6. Snapshot the filesystem again.
log_must $ZFS snapshot $orig@2

#	7. Send a replication stream of the second snapshot to a new filesystem.
log_must eval "$ZFS send -R $orig@2 > $streamfile"
log_must eval "$ZFS recv $dest < $streamfile"

log_pass "refquota is properly sent-and-received, regardless of any " \
	"intermediate snapshots sent by a replication stream."
