#! /bin/ksh -p
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
# An archive of a zfs file system and an archive of its snapshot
# is identical even though the original file system has
# changed since the snapshot was taken.
#
# STRATEGY:
# 1) Create files in all of the zfs file systems
# 2) Create a tarball of the file system
# 3) Create a snapshot of the dataset
# 4) Remove all the files in the original file system
# 5) Create a tarball of the snapshot
# 6) Extract each tarball and compare directory structures
#

verify_runnable "both"

function cleanup
{
	[ -d $CWD ] && log_must cd $CWD

	snapexists $SNAPFS && log_must zfs destroy $SNAPFS

	[ -e $SNAPDIR ] && log_must rm -rf $SNAPDIR
	[ -e $TESTDIR ] && log_must rm -rf $TESTDIR/*
	[ -d "$SNAPSHOT_TARDIR" ] && log_must rm -rf $SNAPSHOT_TARDIR
}

log_assert "Verify an archive of a file system is identical to " \
    "an archive of its snapshot."

SNAPSHOT_TARDIR="$(mktemp -d /tmp/zfstests_snapshot_002.XXXXXX)"
log_onexit cleanup

typeset -i COUNT=21
typeset OP=create

[ -n $TESTDIR ] && rm -rf $TESTDIR/*

log_note "Create files in the zfs filesystem..."

typeset i=1
while [ $i -lt $COUNT ]; do
	log_must file_write -o $OP -f $TESTDIR/file$i \
	    -b $BLOCKSZ -c $NUM_WRITES -d $DATA
	(( i = i + 1 ))
done

log_note "Create a tarball from $TESTDIR contents..."
CWD=$PWD
log_must cd $TESTDIR
log_must tar cf $SNAPSHOT_TARDIR/original.tar .
log_must cd $CWD

log_note "Create a snapshot and mount it..."
log_must zfs snapshot $SNAPFS

log_note "Remove all of the original files..."
log_must rm -f $TESTDIR/file*

log_note "Create tarball of snapshot..."
CWD=$PWD
log_must cd $SNAPDIR
log_must tar cf $SNAPSHOT_TARDIR/snapshot.tar .
log_must cd $CWD

log_must mkdir $TESTDIR/original $TESTDIR/snapshot

CWD=$PWD
log_must cd $TESTDIR/original
log_must tar xf $SNAPSHOT_TARDIR/original.tar

log_must cd $TESTDIR/snapshot
log_must tar xf $SNAPSHOT_TARDIR/snapshot.tar

log_must cd $CWD

log_must directory_diff $TESTDIR/original $TESTDIR/snapshot
log_pass "Directory structures match."
