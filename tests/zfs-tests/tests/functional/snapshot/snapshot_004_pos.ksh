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
	snapexists $SNAPFS && log_must zfs destroy $SNAPFS

	[ -e $TESTDIR ] && log_must rm -rf $TESTDIR/*
}

log_assert "Verify that a snapshot of an empty file system remains empty."

log_onexit cleanup

[ -n $TESTDIR ] && log_must rm -rf $TESTDIR/*

log_must zfs snapshot $SNAPFS
FILE_COUNT=$(ls -A $SNAPDIR | wc -l)
if [[ $FILE_COUNT -ne 0 ]]; then
	ls $SNAPDIR
	log_fail "BEFORE: $SNAPDIR contains $FILE_COUNT files(s)."
fi

typeset -i COUNT=10

log_note "Populate the $TESTDIR directory"
typeset -i i=1
while [[ $i -lt $COUNT ]]; do
	log_must file_write -o create -f $TESTDIR/file$i \
	   -b $BLOCKSZ -c $NUM_WRITES -d $i

	(( i = i + 1 ))
done

FILE_COUNT=$(ls -A $SNAPDIR | wc -l)
if [[ $FILE_COUNT -ne 0 ]]; then
        ls $SNAPDIR
        log_fail "AFTER: $SNAPDIR contains $FILE_COUNT files(s)."
fi

log_pass "The NULL snapshot remains empty."
