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
# Verify that many snapshots can be made on a zfs file system.
#
# STRATEGY:
# 1) Create a files in the zfs file system
# 2) Create a snapshot of the dataset
# 3) Remove all the files from the original file system
# 4) Verify consistency of each snapshot directory
#

verify_runnable "both"

function cleanup
{
	typeset -i i=1
	while [ $i -lt $COUNT ]; do
		snapexists $SNAPFS.$i && log_must zfs destroy $SNAPFS.$i
		(( i = i + 1 ))
	done

	if [ -e $TESTDIR ]; then
		log_must rm -rf $TESTDIR/*
	fi
}

log_assert "Verify many snapshots of a file system can be taken."

log_onexit cleanup

[ -n $TESTDIR ] && log_must rm -rf $TESTDIR/*

typeset -i COUNT=10

log_note "Create some files in the $TESTDIR directory..."
typeset -i i=1
while [[ $i -lt $COUNT ]]; do
	log_must file_write -o create -f $TESTDIR/file$i \
	   -b $BLOCKSZ -c $NUM_WRITES -d $i
	log_must zfs snapshot $SNAPFS.$i

	(( i = i + 1 ))
done

log_note "Remove all of the original files"
[ -n $TESTDIR ] && log_must rm -rf $TESTDIR/file*

i=1
while [[ $i -lt $COUNT ]]; do
	FILECOUNT=$(ls $SNAPDIR.$i/file* | wc -l)
	typeset j=1
	while [ $j -lt $FILECOUNT ]; do
		log_must file_check $SNAPDIR.$i/file$j $j
		(( j = j + 1 ))
	done
	(( i = i + 1 ))
done

log_pass "All files are consistent"
