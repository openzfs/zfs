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
# Copyright (c) 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Verify that umount and destroy fail, and do not unshare the shared
# file system
#
# STRATEGY:
# 1. Share the filesystem via 'zfs set sharenfs'.
# 2. Try umount failure, and verify that the file system is still shared.
# 3. Try destroy failure, and verify that the file system is still shared.
#

verify_runnable "global"

function cleanup
{
	log_must cd $origdir

	log_must zfs set sharenfs=off $TESTPOOL/$TESTFS
	unshare_fs $TESTPOOL/$TESTFS

	snapexists "$TESTPOOL/$TESTFS@snapshot" && \
		destroy_dataset $TESTPOOL/$TESTFS@snapshot -f

	datasetexists $TESTPOOL/$TESTFS/fs2 && \
		destroy_dataset $TESTPOOL/$TESTFS/fs2 -f
}

log_assert "Verify that umount and destroy fail, and do not unshare the shared" \
	"file system"
log_onexit cleanup

typeset origdir=$PWD

# unmount fails will not unshare the shared filesystem
log_must zfs set sharenfs=on $TESTPOOL/$TESTFS
log_must is_shared $TESTDIR
log_must cd $TESTDIR
log_mustnot zfs umount $TESTPOOL/$TESTFS
log_must is_shared $TESTDIR

# destroy fails will not unshare the shared filesystem
log_must zfs create $TESTPOOL/$TESTFS/fs2
log_must cd $TESTDIR/fs2
log_mustnot zfs destroy $TESTPOOL/$TESTFS/fs2
log_must is_shared $TESTDIR/fs2

log_pass "Verify that umount and destroy fail, and do not unshare the shared" \
	"file system"
