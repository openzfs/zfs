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
