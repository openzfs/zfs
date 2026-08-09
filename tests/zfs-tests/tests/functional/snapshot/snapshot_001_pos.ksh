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
# A zfs file system snapshot is identical to
# the originally snapshotted file system, after the file
# system has been changed. Uses 'cksum'.
#
# STRATEGY:
# 1. Create a file in the zfs file system
# 2. Checksum the file for later comparison
# 3. Create a snapshot of the dataset
# 4. Append to the original file
# 5. Verify the snapshot and file agree
#

verify_runnable "both"

function cleanup
{
	if snapexists $SNAPFS; then
		log_must zfs destroy $SNAPFS
	fi
	log_must rm -rf $TESTDIR/*
}

log_assert "Verify a file system snapshot is identical to original."

log_onexit cleanup

log_note "Create a file in the zfs filesystem..."
log_must file_write -o create -f $TESTDIR/$TESTFILE -b $BLOCKSZ \
    -c $NUM_WRITES -d $DATA

log_note "Sum the file, save for later comparison..."
read -r FILE_SUM _ < <(cksum $TESTDIR/$TESTFILE)
log_note "FILE_SUM = $FILE_SUM"

log_note "Create a snapshot and mount it..."
log_must zfs snapshot $SNAPFS

log_note "Append to the original file..."
log_must file_write -o append -f $TESTDIR/$TESTFILE -b $BLOCKSZ \
    -c $NUM_WRITES -d $DATA

read -r SNAP_FILE_SUM _ < <(cksum $SNAPDIR/$TESTFILE)
if [[ $SNAP_FILE_SUM -ne $FILE_SUM ]]; then
	log_fail "Sums do not match, aborting!! ($SNAP_FILE_SUM != $FILE_SUM)"
fi

log_pass "Both Sums match. ($SNAP_FILE_SUM == $FILE_SUM)"
