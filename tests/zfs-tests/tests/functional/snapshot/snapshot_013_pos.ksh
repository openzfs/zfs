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
#	verify that the snapshots created by 'snapshot -r' can be used for
#	zfs send/recv
#
# STRATEGY:
#	1. create a dataset tree and populate a filesystem
#	2. snapshot -r the dataset tree
#	3. select one snapshot used  for zfs send/recv
#	4. verify the data integrity after zfs send/recv
#

verify_runnable "both"

function cleanup
{
	datasetexists $ctrfs && destroy_dataset $ctrfs -r
	snapexists $snappool && destroy_dataset $snappool -r

	[ -e $TESTDIR ] && log_must rm -rf $TESTDIR/*
}

log_assert "Verify snapshots from 'snapshot -r' can be used for zfs send/recv"
log_onexit cleanup

ctr=$TESTPOOL/$TESTCTR
ctrfs=$ctr/$TESTFS
snappool=$SNAPPOOL
snapfs=$SNAPFS
snapctr=$ctr@$TESTSNAP
snapctrfs=$ctrfs@$TESTSNAP
fsdir=/$ctrfs
snapdir=$fsdir/.zfs/snapshot/$TESTSNAP

[ -n $TESTDIR ] && log_must rm -rf $TESTDIR/*

typeset -i COUNT=10

log_note "Populate the $TESTDIR directory (prior to snapshot)"
typeset -i i=0
while (( i < COUNT )); do
	log_must file_write -o create -f $TESTDIR/file$i \
	   -b $BLOCKSZ -c $NUM_WRITES -d $i

	(( i = i + 1 ))
done

log_must zfs snapshot -r $snappool

zfs send $snapfs | zfs receive $ctrfs >/dev/null 2>&1
if ! datasetexists $ctrfs || ! snapexists $snapctrfs; then
	log_fail "zfs send/receive fails with snapshot $snapfs."
fi

for dir in $fsdir $snapdir; do
	FILE_COUNT=$(ls -A $dir | wc -l)
	(( FILE_COUNT != COUNT )) && log_fail "Got $FILE_COUNT expected $COUNT"
done

log_pass "'zfs send/receive' works as expected with snapshots from 'snapshot -r'"
