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
# Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#	refquotas are not limited by snapshots.
#
# STRATEGY:
#	1. Setting refquota < quota
#	2. Create file in filesystem, take snapshot and remove the file
#	3. Verify snapshot will not consume refquota
#

verify_runnable "both"

function cleanup
{
	log_must zfs destroy -rf $TESTPOOL/$TESTFS
	log_must zfs create $TESTPOOL/$TESTFS
	log_must zfs set mountpoint=$TESTDIR $TESTPOOL/$TESTFS
}

log_assert "refquotas are not limited by snapshots."
log_onexit cleanup

TESTFILE='testfile'
fs=$TESTPOOL/$TESTFS
log_must zfs set quota=25M $fs
log_must zfs set refquota=15M $fs

mntpnt=$(get_prop mountpoint $fs)
typeset -i i=0
while ((i < 3)); do
	log_must mkfile 7M $mntpnt/$TESTFILE.$i
	log_must zfs snapshot $fs@snap.$i
	log_must rm $mntpnt/$TESTFILE.$i

	((i += 1))
done

#
# Verify out of the limitation of 'quota'
#
log_mustnot mkfile 7M $mntpnt/$TESTFILE

log_pass "refquotas are not limited by snapshots."
