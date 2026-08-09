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
# Populate a file system and take a snapshot. Add some more files to the
# file system and rollback to the last snapshot. Verify no post snapshot
# file exist.
#
# STRATEGY:
# 1. Empty a file system
# 2. Populate the file system
# 3. Take a snapshot of the file system
# 4. Add new files to the file system
# 5. Perform a rollback
# 6. Verify the snapshot and file system agree
#

verify_runnable "both"

function cleanup
{
	snapexists $SNAPFS &&
		log_must zfs destroy $SNAPFS

	[ -e $TESTDIR ] && log_must rm -rf $TESTDIR/*
}

log_assert "Verify that a rollback to a previous snapshot succeeds."

log_onexit cleanup

[ -n $TESTDIR ] && log_must rm -rf $TESTDIR/*

typeset -i COUNT=10

log_note "Populate the $TESTDIR directory (prior to snapshot)"
typeset -i i=1
while [[ $i -le $COUNT ]]; do
	log_must file_write -o create -f $TESTDIR/before_file$i \
	   -b $BLOCKSZ -c $NUM_WRITES -d $i

	(( i = i + 1 ))
done

log_must zfs snapshot $SNAPFS

FILE_COUNT=$(ls -A $SNAPDIR | wc -l)
if [[ $FILE_COUNT -ne $COUNT ]]; then
        ls -Al $SNAPDIR
        log_fail "AFTER: $SNAPFS contains $FILE_COUNT files(s)."
fi

log_note "Populate the $TESTDIR directory (post snapshot)"
typeset -i i=1
while [[ $i -le $COUNT ]]; do
        log_must file_write -o create -f $TESTDIR/after_file$i \
           -b $BLOCKSZ -c $NUM_WRITES -d $i

        (( i = i + 1 ))
done
sync_pool $TESTPOOL

#
# Now rollback to latest snapshot
#
log_must zfs rollback $SNAPFS

FILE_COUNT=$(ls -A $TESTDIR/after* 2> /dev/null | wc -l)
if [[ $FILE_COUNT -ne 0 ]]; then
        ls -Al $TESTDIR
        log_fail "$TESTDIR contains $FILE_COUNT after* files(s)."
fi

FILE_COUNT=$(ls -A $TESTDIR/before* 2> /dev/null | wc -l)
if [[ $FILE_COUNT -ne $COUNT ]]; then
	ls -Al $TESTDIR
	log_fail "$TESTDIR contains $FILE_COUNT before* files(s)."
fi

log_pass "The rollback operation succeeded."
