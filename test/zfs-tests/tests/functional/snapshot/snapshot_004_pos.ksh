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
# Copyright (c) 2013 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/snapshot/snapshot.cfg

#
# DESCRIPTION:
# Create a null snapshot i.e. a snapshot created before file system
# activity is empty.
#
# STRATEGY:
# 1. Empty a file system
# 2. Take a snapshot of the empty file system.
# 3. Populate the file system
# 4. Verify the snapshot is still empty
#

verify_runnable "both"

function cleanup
{
	snapexists $SNAPFS
	[[ $? -eq 0 ]] && \
		log_must $ZFS destroy $SNAPFS

	[[ -e $TESTDIR ]] && \
		log_must $RM -rf $TESTDIR/* > /dev/null 2>&1
}

log_assert "Verify that a snapshot of an empty file system remains empty."

log_onexit cleanup

[[ -n $TESTDIR ]] && \
    log_must $RM -rf $TESTDIR/* > /dev/null 2>&1

log_must $ZFS snapshot $SNAPFS
FILE_COUNT=`$LS -Al $SNAPDIR | $GREP -v "total 0" | wc -l`
if [[ $FILE_COUNT -ne 0 ]]; then
	$LS $SNAPDIR
	log_fail "BEFORE: $SNAPDIR contains $FILE_COUNT files(s)."
fi

typeset -i COUNT=10

log_note "Populate the $TESTDIR directory"
typeset -i i=1
while [[ $i -lt $COUNT ]]; do
	log_must $FILE_WRITE -o create -f $TESTDIR/file$i \
	   -b $BLOCKSZ -c $NUM_WRITES -d $i

	(( i = i + 1 ))
done

FILE_COUNT=`$LS -Al $SNAPDIR | $GREP -v "total 0" | wc -l`
if [[ $FILE_COUNT -ne 0 ]]; then
        $LS $SNAPDIR
        log_fail "AFTER: $SNAPDIR contains $FILE_COUNT files(s)."
fi

log_pass "The NULL snapshot remains empty."
