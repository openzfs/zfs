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
#	use 'snapshot -r' to create a snapshot tree, add some files to one child
#	filesystem, rollback the child filesystem snapshot, verify that the child
# 	filesystem gets back to the status while taking the snapshot.
#
# STRATEGY:
#	1. Add some files to a target child filesystem
#	2. snapshot -r the parent filesystem
#	3. Add some other files to the target child filesystem
#	4. rollback the child filesystem snapshot
#	5. verify that the child filesystem get back to the status while being
#	   snapshot'd
#

verify_runnable "both"

function cleanup
{
	snapexists $SNAPPOOL && destroy_dataset $SNAPPOOL -r

	[ -e $TESTDIR ] && log_must rm -rf $TESTDIR/*
}

log_assert "Verify that rollback to a snapshot created by snapshot -r succeeds."
log_onexit cleanup

[ -n $TESTDIR ] && log_must rm -rf $TESTDIR/*

typeset -i COUNT=10

log_note "Populate the $TESTDIR directory (prior to snapshot)"
typeset -i i=0
while (( i < COUNT )); do
	log_must file_write -o create -f $TESTDIR/before_file$i \
	   -b $BLOCKSZ -c $NUM_WRITES -d $i

	(( i = i + 1 ))
done

log_must zfs snapshot -r $SNAPPOOL

FILE_COUNT=$(ls -A $SNAPDIR | wc -l)
if (( FILE_COUNT != COUNT )); then
        ls -Al $SNAPDIR
        log_fail "AFTER: $SNAPFS contains $FILE_COUNT files(s)."
fi

log_note "Populate the $TESTDIR directory (post snapshot)"
typeset -i i=0
while (( i < COUNT )); do
        log_must file_write -o create -f $TESTDIR/after_file$i \
           -b $BLOCKSZ -c $NUM_WRITES -d $i

        (( i = i + 1 ))
done

#
# Now rollback to latest snapshot
#
log_must zfs rollback $SNAPFS

FILE_COUNT=$(ls -A $TESTDIR/after* 2> /dev/null | wc -l)
if (( FILE_COUNT != 0 )); then
        ls -Al $TESTDIR
        log_fail "$TESTDIR contains $FILE_COUNT after* files(s)."
fi

FILE_COUNT=$(ls -A $TESTDIR/before* 2> /dev/null | wc -l)
if (( FILE_COUNT != $COUNT )); then
	ls -Al $TESTDIR
	log_fail "$TESTDIR contains $FILE_COUNT before* files(s)."
fi

log_pass "Rollback with child snapshot works as expected."
