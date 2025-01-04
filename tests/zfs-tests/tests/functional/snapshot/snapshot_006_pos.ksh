#! /bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or https://opensource.org/licenses/CDDL-1.0.
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

	if [ -e $SNAPDIR1 ]; then
		log_must rm -rf $SNAPDIR1
	fi

	if [ -e $TESTDIR1 ]; then
		log_must rm -rf $TESTDIR1/*
	fi

	if [ -d "$SNAPSHOT_TARDIR" ]; then
		log_must rm -rf $SNAPSHOT_TARDIR
	fi
}

log_assert "Verify that an archive of a dataset is identical to " \
   "an archive of the dataset's snapshot."

SNAPSHOT_TARDIR="$(mktemp -d /tmp/zfstests_snapshot_006.XXXXXX)"
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
