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
		snapexists $SNAPFS.$i
		if [[ $? -eq 0 ]]; then
			log_must zfs destroy $SNAPFS.$i
		fi

		if [[ -e $SNAPDIR.$i ]]; then
			log_must rm -rf $SNAPDIR.$i > /dev/null 2>&1
		fi

		(( i = i + 1 ))
	done

	if [[ -e $TESTDIR ]]; then
		log_must rm -rf $TESTDIR/* > /dev/null 2>&1
	fi
}

log_assert "Verify many snapshots of a file system can be taken."

log_onexit cleanup

[[ -n $TESTDIR ]] && \
    log_must rm -rf $TESTDIR/* > /dev/null 2>&1

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
[[ -n $TESTDIR ]] && \
    log_must rm -rf $TESTDIR/file* > /dev/null 2>&1

i=1
while [[ $i -lt $COUNT ]]; do
	FILECOUNT=`ls $SNAPDIR.$i/file* | wc -l`
	typeset j=1
	while [ $j -lt $FILECOUNT ]; do
		log_must file_check $SNAPDIR.$i/file$j $j
		(( j = j + 1 ))
	done
	(( i = i + 1 ))
done

log_pass "All files are consistent"
