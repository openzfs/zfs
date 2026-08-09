#! /bin/ksh -p
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
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/snapshot/snapshot.cfg

#
# DESCRIPTION:
#	verify that creating/destroying snapshots do things clean
#
# STRATEGY:
#	1. create a dataset and set a quota with 500m
#	2. create file of size 400m on the dataset
#	3. take a snapshot and destroy it
#	4. then create file to use all spaces in the dataset
#	5. verify removing the first file should succeed
#

verify_runnable "both"

function cleanup
{
	[ -e $TESTDIR1 ] && log_must rm -rf $TESTDIR1/*

	snapexists $SNAPCTR && destroy_dataset $SNAPCTR

	datasetexists $TESTPOOL/$TESTCTR/$TESTFS1 && \
		log_must zfs set quota=none $TESTPOOL/$TESTCTR/$TESTFS1

	zfs inherit compression $TESTPOOL
}

log_assert "Verify creating/destroying snapshots do things clean"
log_onexit cleanup

log_must zfs set compression=off $TESTPOOL

log_must zfs set quota=$FSQUOTA $TESTPOOL/$TESTCTR/$TESTFS1
log_must mkfile $FILESIZE $TESTDIR1/$TESTFILE

log_must zfs snapshot $SNAPCTR
log_must zfs destroy $SNAPCTR

log_note "Make the quota of filesystem is reached"
log_mustnot mkfile $FILESIZE1 $TESTDIR1/$TESTFILE1

log_note "Verify removing the first file should succeed after the snapshot is \
	removed"
log_must rm $TESTDIR1/$TESTFILE

log_pass "Verify creating/destroying snapshots do things clean"
