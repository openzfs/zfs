#! /bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
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

	log_must rm -rf $SNAPDIR $TESTDIR/*
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
