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
	snapexists $SNAPFS
	[[ $? -eq 0 ]] && \
		log_must zfs destroy $SNAPFS

	[[ -e $TESTDIR ]] && \
		log_must rm -rf $TESTDIR/* > /dev/null 2>&1
}

log_assert "Verify that a rollback to a previous snapshot succeeds."

log_onexit cleanup

[[ -n $TESTDIR ]] && \
    log_must rm -rf $TESTDIR/* > /dev/null 2>&1

typeset -i COUNT=10

log_note "Populate the $TESTDIR directory (prior to snapshot)"
typeset -i i=1
while [[ $i -le $COUNT ]]; do
	log_must file_write -o create -f $TESTDIR/before_file$i \
	   -b $BLOCKSZ -c $NUM_WRITES -d $i

	(( i = i + 1 ))
done

log_must zfs snapshot $SNAPFS

FILE_COUNT=`ls -Al $SNAPDIR | grep -v "total" | wc -l`
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

#
# Now rollback to latest snapshot
#
log_must zfs rollback $SNAPFS

FILE_COUNT=`ls -Al $TESTDIR/after* 2> /dev/null | grep -v "total" | wc -l`
if [[ $FILE_COUNT -ne 0 ]]; then
        ls -Al $TESTDIR
        log_fail "$TESTDIR contains $FILE_COUNT after* files(s)."
fi

FILE_COUNT=`ls -Al $TESTDIR/before* 2> /dev/null \
    | grep -v "total" | wc -l`
if [[ $FILE_COUNT -ne $COUNT ]]; then
	ls -Al $TESTDIR
	log_fail "$TESTDIR contains $FILE_COUNT before* files(s)."
fi

log_pass "The rollback operation succeeded."
