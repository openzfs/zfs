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

. $STF_SUITE/tests/functional/rsend/rsend.kshlib

#
# DESCRIPTION:
#	zfs receive can handle out of space correctly.
#
# STRATEGY:
#	1. Create two pools, one is big and another is small.
#	2. Fill the big pool with data.
#	3. Take snapshot and backup the whole pool.
#	4. Receive this stream in small pool.
#	5. Verify zfs receive can handle the out of space error correctly.
#

verify_runnable "global"

BPOOL=bpool_test
SPOOL=spool_test

function cleanup
{
	if datasetexists $BPOOL ; then
		log_must_busy zpool destroy -f $BPOOL
	fi
	if datasetexists $SPOOL ; then
		log_must_busy zpool destroy -f $SPOOL
	fi
}

log_assert "Verify zfs receive can handle out of space correctly."
log_onexit cleanup

log_must mkfile $MINVDEVSIZE $TESTDIR/bfile
log_must mkfile $SPA_MINDEVSIZE  $TESTDIR/sfile
log_must zpool create -O compression=off $BPOOL $TESTDIR/bfile
log_must zpool create -O compression=off $SPOOL $TESTDIR/sfile

#
# Test out of space on sub-filesystem
#
log_must zfs create $BPOOL/fs
log_must mkfile 30M /$BPOOL/fs/file

log_must zfs snapshot $BPOOL/fs@snap
log_must eval "zfs send -R $BPOOL/fs@snap > $BACKDIR/fs-R"
log_mustnot eval "zfs receive -d -F $SPOOL < $BACKDIR/fs-R"

log_must datasetnonexists $SPOOL/fs
log_must ismounted $SPOOL

#
# Test out of space on top filesystem
#
log_must mv /$BPOOL/fs/file /$BPOOL
log_must_busy zfs destroy -rf $BPOOL/fs

log_must zfs snapshot $BPOOL@snap
log_must eval "zfs send -R $BPOOL@snap > $BACKDIR/bpool-R"
log_mustnot eval "zfs receive -d -F $SPOOL < $BACKDIR/bpool-R"

log_must datasetnonexists $SPOOL/fs
log_must ismounted $SPOOL

log_pass "zfs receive can handle out of space correctly."
