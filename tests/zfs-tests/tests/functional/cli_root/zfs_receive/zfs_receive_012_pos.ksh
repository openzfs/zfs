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
# Copyright 2016, OmniTI Computer Consulting, Inc. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#	refquota, like regular quota, is loosely enforced.  A dataset
#	can exceed its refquota by one transaction.  This loose enforcement
#	used to cause problems upon receiving a datastream where its
#	refquota is slightly exceeded.  This test confirms that we can
#	successfully receive a slightly over refquota stream.
#
# STRATEGY:
#	1. Create a filesystem.
#	2. Set a refquota.
#	3. Snapshot the filesystem.
#	4. Send a replication stream to a new filesystem.
#	5. On the original filesystem, fill it up to its quota.
#	6. Snapshot the original filesystem again.
#	7. Send an incremental stream to the same new filesystem.
#

verify_runnable "both"

typeset streamfile=/var/tmp/streamfile.$$

function cleanup
{
	log_must $RM $streamfile
	log_must $ZFS destroy -rf $TESTPOOL/$TESTFS1
	log_must $ZFS destroy -rf $TESTPOOL/$TESTFS2
}

log_assert "The allowable slight refquota overage is properly sent-and-" \
	"received."
log_onexit cleanup

orig=$TESTPOOL/$TESTFS1
dest=$TESTPOOL/$TESTFS2

#	1. Create a filesystem.
log_must $ZFS create $orig
origdir=$(get_prop mountpoint $orig)

#	2. Set a refquota.
log_must $ZFS set refquota=50M $orig

#	3. Snapshot the filesystem.
log_must $ZFS snapshot $orig@1

#	4. Send a replication stream to a new filesystem.
log_must eval "$ZFS send -R $orig@1 > $streamfile"
log_must eval "$ZFS recv $dest < $streamfile"

#	5. On the original filesystem, fill it up to its quota.
cat < /dev/urandom > $origdir/fill-it-up

#	6. Snapshot the original filesystem again.
log_must $ZFS snapshot $orig@2

#	7. Send an incremental stream to the same new filesystem.
log_must eval "$ZFS send -I 1 -R $orig@2 > $streamfile"
log_must eval "$ZFS recv $dest < $streamfile"

log_pass "Verified receiving a slightly-over-refquota stream succeeds."
