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
#	refquota limits the amount of space a dataset can consume, but does
#	not include space used by descendents.
#
# STRATEGY:
#	1. Setting refquota in given filesystem
#	2. Create descendent filesystem
#	3. Verify refquota limits the amount of space a dataset can consume
#	4. Verify the limit does not impact descendents
#

verify_runnable "both"

function cleanup
{
	log_must zfs destroy -rf $TESTPOOL/$TESTFS
	log_must zfs create $TESTPOOL/$TESTFS
	log_must zfs set mountpoint=$TESTDIR $TESTPOOL/$TESTFS
}

log_assert "refquota limits the amount of space a dataset can consume, " \
	"but does not include space used by descendents."
log_onexit cleanup

fs=$TESTPOOL/$TESTFS
sub=$fs/sub
log_must zfs create $sub

log_must zfs set refquota=10M $fs
mntpnt=$(get_prop mountpoint $fs)

log_mustnot mkfile 11M $mntpnt/file
log_must mkfile 9M $mntpnt/file
log_must zfs snapshot $fs@snap
log_mustnot mkfile 2M $mntpnt/file2

mntpnt=$(get_prop mountpoint $sub)
log_must mkfile 10M $mntpnt/file
log_must zfs snapshot $sub@snap
log_must mkfile 10 $mntpnt/file2

log_pass "refquota limits the amount of space a dataset can consume, " \
	"but does not include space used by descendents."
