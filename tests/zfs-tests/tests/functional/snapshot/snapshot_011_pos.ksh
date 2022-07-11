#! /bin/ksh -p
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
