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
# An archive of a zfs dataset and an archive of its snapshot
# changed since the snapshot was taken.
#
# STRATEGY:
# 1) Create some files in a ZFS dataset
# 2) Create a tarball of the dataset
# 3) Create a snapshot of the dataset
# 4) Remove all the files in the original dataset
# 5) Create a tarball of the snapshot
# 6) Extract each tarball and compare directory structures
#

verify_runnable "both"

function cleanup
{
	if [[ -d $CWD ]]; then
		log_must cd $CWD
	fi

	snapexists $SNAPCTR && log_must zfs destroy $SNAPCTR

	if [ -e $TESTDIR1 ]; then
		log_must rm -rf $TESTDIR1/*
	fi

	if [ -d "$SNAPSHOT_TARDIR" ]; then
		log_must rm -rf $SNAPSHOT_TARDIR
	fi
}

log_assert "Verify that an archive of a dataset is identical to " \
   "an archive of the dataset's snapshot."

SNAPSHOT_TARDIR="$(mktemp -d "$TEST_BASE_DIR/zfstests_snapshot_006.XXXXXX")"
log_onexit cleanup

typeset -i COUNT=21
typeset OP=create

[ -n $TESTDIR1 ] && rm -rf $TESTDIR1/*

log_note "Create files in the zfs dataset ..."

typeset i=1
while [ $i -lt $COUNT ]; do
	log_must file_write -o $OP -f $TESTDIR1/file$i \
	    -b $BLOCKSZ -c $NUM_WRITES -d $DATA
	(( i = i + 1 ))
done

log_note "Create a tarball from $TESTDIR1 contents..."
CWD=$PWD
log_must cd $TESTDIR1
log_must tar cf $SNAPSHOT_TARDIR/original.tar .
log_must cd $CWD

log_note "Create a snapshot and mount it..."
log_must zfs snapshot $SNAPCTR

log_note "Remove all of the original files..."
log_must rm -f $TESTDIR1/file*

log_note "Create tarball of snapshot..."
CWD=$PWD
log_must cd $SNAPDIR1
log_must tar cf $SNAPSHOT_TARDIR/snapshot.tar .
log_must cd $CWD

log_must mkdir $TESTDIR1/original mkdir $TESTDIR1/snapshot

CWD=$PWD
log_must cd $TESTDIR1/original
log_must tar xf $SNAPSHOT_TARDIR/original.tar

log_must cd $TESTDIR1/snapshot
log_must tar xf $SNAPSHOT_TARDIR/snapshot.tar

log_must cd $CWD

log_must directory_diff $TESTDIR1/original $TESTDIR1/snapshot
log_pass "Directory structures match."
