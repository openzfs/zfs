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
	if [[ -d $CWD ]]; then
		cd $CWD || log_fail "Could not cd $CWD"
	fi

        snapexists $SNAPFS
        if [[ $? -eq 0 ]]; then
                log_must zfs destroy $SNAPFS
        fi

        if [[ -e $SNAPDIR ]]; then
                log_must rm -rf $SNAPDIR > /dev/null 2>&1
        fi

        if [[ -e $TESTDIR ]]; then
                log_must rm -rf $TESTDIR/* > /dev/null 2>&1
        fi

}

log_assert "Verify an archive of a file system is identical to " \
    "an archive of its snapshot."

log_onexit cleanup

typeset -i COUNT=21
typeset OP=create

[[ -n $TESTDIR ]] && \
    rm -rf $TESTDIR/* > /dev/null 2>&1

log_note "Create files in the zfs filesystem..."

typeset i=1
while [ $i -lt $COUNT ]; do
	log_must file_write -o $OP -f $TESTDIR/file$i \
	    -b $BLOCKSZ -c $NUM_WRITES -d $DATA

	(( i = i + 1 ))
done

log_note "Create a tarball from $TESTDIR contents..."
CWD=$PWD
cd $TESTDIR || log_fail "Could not cd $TESTDIR"
log_must tar cf $TESTDIR/tarball.original.tar file*
cd $CWD || log_fail "Could not cd $CWD"

log_note "Create a snapshot and mount it..."
log_must zfs snapshot $SNAPFS

log_note "Remove all of the original files..."
log_must rm -f $TESTDIR/file* > /dev/null 2>&1

log_note "Create tarball of snapshot..."
CWD=$PWD
cd $SNAPDIR || log_fail "Could not cd $SNAPDIR"
log_must tar cf $TESTDIR/tarball.snapshot.tar file*
cd $CWD || log_fail "Could not cd $CWD"

log_must mkdir $TESTDIR/original
log_must mkdir $TESTDIR/snapshot

CWD=$PWD
cd $TESTDIR/original || log_fail "Could not cd $TESTDIR/original"
log_must tar xf $TESTDIR/tarball.original.tar

cd $TESTDIR/snapshot || log_fail "Could not cd $TESTDIR/snapshot"
log_must tar xf $TESTDIR/tarball.snapshot.tar

cd $CWD || log_fail "Could not cd $CWD"

diff -q -r $TESTDIR/original $TESTDIR/snapshot > /dev/null 2>&1
if [[ $? -eq 1 ]]; then
	log_fail "Directory structures differ."
fi

log_pass "Directory structures match."
