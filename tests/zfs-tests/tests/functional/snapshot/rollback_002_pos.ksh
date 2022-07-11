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
# Verify that rollbacks are with respect to the latest snapshot.
#
# STRATEGY:
# 1. Empty a file system
# 2. Populate the file system
# 3. Take a snapshot of the file system
# 4. Add new files to the file system
# 5. Take a snapshot
# 6. Remove the original files
# 7. Perform a rollback
# 8. Verify the latest snapshot and file system agree
#

verify_runnable "both"

function cleanup
{
	snapexists $SNAPFS.1 &&
		log_must zfs destroy $SNAPFS.1

	snapexists $SNAPFS &&
		log_must zfs destroy $SNAPFS

	[ -e $TESTDIR ] && log_must rm -rf $TESTDIR/*
}

log_assert "Verify rollback is with respect to latest snapshot."

log_onexit cleanup

[ -n $TESTDIR ] && log_must rm -rf $TESTDIR/*

typeset -i COUNT=10

log_note "Populate the $TESTDIR directory (prior to first snapshot)"
typeset -i i=1
while [[ $i -le $COUNT ]]; do
	log_must file_write -o create -f $TESTDIR/original_file$i \
	   -b $BLOCKSZ -c $NUM_WRITES -d $i

	(( i = i + 1 ))
done

log_must zfs snapshot $SNAPFS

FILE_COUNT=$(ls -A $SNAPDIR | wc -l)
if [[ $FILE_COUNT -ne $COUNT ]]; then
        ls -Al $SNAPDIR
        log_fail "AFTER: $SNAPFS contains $FILE_COUNT files(s)."
fi

log_note "Populate the $TESTDIR directory (prior to second snapshot)"
typeset -i i=1
while [[ $i -le $COUNT ]]; do
        log_must file_write -o create -f $TESTDIR/afterfirst_file$i \
           -b $BLOCKSZ -c $NUM_WRITES -d $i

        (( i = i + 1 ))
done

log_must zfs snapshot $SNAPFS.1

log_note "Populate the $TESTDIR directory (Post second snapshot)"
typeset -i i=1
while [[ $i -le $COUNT ]]; do
        log_must file_write -o create -f $TESTDIR/aftersecond_file$i \
           -b $BLOCKSZ -c $NUM_WRITES -d $i

        (( i = i + 1 ))
done

[ -n $TESTDIR ] && log_must rm -f $TESTDIR/original_file*

#
# Now rollback to latest snapshot
#
log_must zfs rollback $SNAPFS.1

FILE_COUNT=$(ls -A $TESTDIR/aftersecond* 2> /dev/null | wc -l)
if [[ $FILE_COUNT -ne 0 ]]; then
        ls -Al $TESTDIR
        log_fail "$TESTDIR contains $FILE_COUNT aftersecond* files(s)."
fi

FILE_COUNT=$(ls -A $TESTDIR/original* $TESTDIR/afterfirst* | wc -l)
if [[ $FILE_COUNT -ne 20 ]]; then
        ls -Al $TESTDIR
        log_fail "$TESTDIR contains $FILE_COUNT original* files(s)."
fi

log_pass "The rollback to the latest snapshot succeeded."
