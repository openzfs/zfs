#!/bin/ksh -p
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
# Copyright (c) 2007, Sun Microsystems Inc. All rights reserved.
# Copyright (c) 2013, 2016, Delphix. All rights reserved.
# Use is subject to license terms.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/compression/compress.cfg

#
# DESCRIPTION:
# Create two files of exactly the same size. One with compression
# and one without. Ensure the compressed file is smaller.
#
# STRATEGY:
# Use "zfs set" to turn on compression and create files before
# and after the set call. The compressed file should be smaller.
#

verify_runnable "both"

typeset OP=create

log_assert "Ensure that compressed files are smaller."

log_note "Ensure compression is off"
log_must zfs set compression=off $TESTPOOL/$TESTFS

log_note "Writing file without compression..."
log_must file_write -o $OP -f $TESTDIR/$TESTFILE0 -b $BLOCKSZ \
    -c $NUM_WRITES -d $DATA

log_note "Add compression property to the dataset and write another file"
log_must zfs set compression=on $TESTPOOL/$TESTFS

log_must file_write -o $OP -f $TESTDIR/$TESTFILE1 -b $BLOCKSZ \
    -c $NUM_WRITES -d $DATA

sleep 60

FILE0_BLKS=`du -k $TESTDIR/$TESTFILE0 | awk '{print $1}'`
FILE1_BLKS=`du -k $TESTDIR/$TESTFILE1 | awk '{print $1}'`

if [[ $FILE0_BLKS -le $FILE1_BLKS ]]; then
	log_fail "$TESTFILE0 is smaller than $TESTFILE1" \
			"($FILE0_BLKS <= $FILE1_BLKS)"
fi

log_pass "$TESTFILE0 is bigger than $TESTFILE1 ($FILE0_BLKS > $FILE1_BLKS)"
