#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# CDDL HEADER START
#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# http://www.illumos.org/license/CDDL.
#
# CDDL HEADER END
#

#
# Copyright (c) 2012, 2016 by Delphix. All rights reserved.
# Copyright (c) 2018 Datto, Inc. All rights reserved.
#

. $STF_SUITE/tests/functional/rsend/rsend.kshlib
. $STF_SUITE/tests/functional/cli_root/cli_common.kshlib

#
# DESCRIPTION:
#	Verify 'zfs send' can send dataset holds.
#	Verify 'zfs receive' can receive dataset holds.
#
# STRATEGY:
#	1. Create a snapshot.
#	2. Create a full send stream with the fs, including holds.
#	3. Receive the send stream and verify the data integrity.
#	4. Fill in fs with some new data.
#	5. Create an incremental send stream with the fs, including holds.
#	6. Receive the incremental send stream and verify the data integrity.
#	7. Verify the holds have been received as expected.
#	8. Create an incremental snap with no holds, and send that with -h.
#	9. Confirm the snapshot was received as expected.
#	10. Create an incremental snapshot and place a hold on it.
#	11. Receive the incremental stream with -h and verify holds skipped.
#

verify_runnable "both"

function cleanup
{
	eval "zfs holds $init_snap |grep -q hold1-1" &&
	    log_must zfs release hold1-1 $init_snap
	eval "zfs holds $init_snap |grep -q hold1-2" &&
	    log_must zfs release hold1-2 $init_snap
	eval "zfs holds $recv_snap |grep -q hold1-1" &&
	    log_must zfs release hold1-1 $recv_snap
	eval "zfs holds $recv_snap |grep -q hold1-2" &&
	    log_must zfs release hold1-2 $recv_snap
	eval "zfs holds $inc_snap |grep -q hold2-1" &&
	    log_must zfs release hold2-1 $inc_snap
	eval "zfs holds $recv_inc_snap |grep -q hold2-1" &&
	    log_must zfs release hold2-1 $recv_inc_snap
	eval "zfs holds $inc_snap3 |grep -q hold3-1" &&
	    log_must zfs release hold3-1 $inc_snap3

	# destroy datasets
	datasetexists $recv_root/$TESTFS1 &&
	    log_must destroy_dataset "$recv_root/$TESTFS1" "-Rf"
	datasetexists $recv_root && log_must destroy_dataset "$recv_root" "-Rf"
	datasetexists $TESTPOOL/$TESTFS1 && log_must destroy_dataset "$TESTPOOL/$TESTFS1" "-Rf"

	[[ -e $full_bkup ]] && log_must rm -f $full_bkup
	[[ -e $inc_bkup ]] && log_must rm -f $inc_bkup
	[[ -e $inc_data2 ]] && log_must rm -f $inc_data2
	[[ -d $TESTDIR1 ]] && log_must rm -rf $TESTDIR1

}

#
# Check if hold exists on the specified dataset.
#
function check_hold #<snapshot> <hold>
{
	typeset dataset=$1
	typeset hold=$2

	log_note "checking $dataset for $hold"
	eval "zfs holds $dataset |grep -q $hold"
}

log_assert "Verify 'zfs send/recv' can send and receive dataset holds."
log_onexit cleanup

init_snap=$TESTPOOL/$TESTFS1@init_snap
inc_snap=$TESTPOOL/$TESTFS1@inc_snap
inc_snap2=$TESTPOOL/$TESTFS1@inc_snap2
inc_snap3=$TESTPOOL/$TESTFS1@inc_snap3
full_bkup=$TEST_BASE_DIR/fullbkup.$$
inc_bkup=$TEST_BASE_DIR/incbkup.$$
init_data=$TESTDIR/$TESTFILE1
inc_data=$TESTDIR/$TESTFILE2
inc_data2=$TESTDIR/testfile3
recv_root=$TESTPOOL/rst_ctr
recv_snap=$recv_root/$TESTFS1@init_snap
recv_inc_snap=$recv_root/$TESTFS1@inc_snap
recv_inc_snap2=$recv_root/$TESTFS1@inc_snap2
recv_inc_snap3=$recv_root/$TESTFS1@inc_snap3
recv_data=$TESTDIR1/$TESTFS1/$TESTFILE1
recv_inc_data=$TESTDIR1/$TESTFS1/$TESTFILE2
recv_inc_data2=$TESTDIR1/$TESTFS1/testfile3

log_note "Verify 'zfs send' can create full send stream."

# Preparation
if ! datasetexists $TESTPOOL/$TESTFS1; then
	log_must zfs create $TESTPOOL/$TESTFS1
fi
[[ -e $init_data ]] && log_must rm -f $init_data
log_must zfs create $recv_root
[[ ! -d $TESTDIR1 ]] && log_must mkdir -p $TESTDIR1
[[ ! -d $TESTDIR ]] && log_must mkdir -p $TESTDIR
log_must zfs set mountpoint=$TESTDIR $TESTPOOL/$TESTFS1
log_must zfs set mountpoint=$TESTDIR1 $recv_root

file_write -o create -f $init_data -b $BLOCK_SIZE -c $WRITE_COUNT

log_must zfs snapshot $init_snap
log_must zfs hold hold1-1 $init_snap
log_must zfs hold hold1-2 $init_snap
log_must eval "zfs send -h $init_snap > $full_bkup"

log_note "Verify the send stream is valid to receive."

log_must zfs recv -F $recv_snap <$full_bkup
log_must datasetexists $recv_snap
receive_check $recv_snap ${recv_snap%%@*}

log_note "Verify the holds were received."
log_must check_hold $recv_snap hold1-1
log_must check_hold $recv_snap hold1-2
compare_cksum $init_data $recv_data

log_note "Verify 'zfs send -i' can create incremental send stream."

file_write -o create -f $inc_data -b $BLOCK_SIZE -c $WRITE_COUNT -d 0

log_must zfs snapshot $inc_snap
log_must zfs hold hold2-1 $inc_snap
log_must eval "zfs send -h -i $init_snap $inc_snap > $inc_bkup"

log_note "Verify the incremental send stream is valid to receive."

log_must zfs recv -F $recv_inc_snap <$inc_bkup
log_must datasetexists $recv_inc_snap
log_note "Verify the hold was received from the incremental send."

log_must check_hold $recv_inc_snap hold2-1

compare_cksum $inc_data $recv_inc_data

log_note "Verify send -h works when there are no holds."
file_write -o create -f $inc_data2 -b $BLOCK_SIZE -c $WRITE_COUNT -d 0
log_must zfs snapshot $inc_snap2
log_must eval "zfs send -h -i $inc_snap $inc_snap2 > $inc_bkup"
log_must zfs recv -F $recv_inc_snap2 <$inc_bkup
log_must datasetexists $recv_inc_snap2
compare_cksum $inc_data2 $recv_inc_data2

log_note "Verify send -h fails properly when receive dataset already exists"
log_must zfs recv -F $recv_inc_snap2 <$inc_bkup

log_note "Verify recv -h skips the receive of holds"
log_must zfs snapshot $inc_snap3
log_must zfs hold hold3-1 $inc_snap3
log_must eval "zfs send -h -i $inc_snap2 $inc_snap3 > $inc_bkup"
log_must zfs recv -F -h $recv_inc_snap3 <$inc_bkup
log_must datasetexists $recv_inc_snap3
log_mustnot check_hold $recv_inc_snap3 hold3-1

log_pass "'zfs send/recv' can send and receive dataset holds."
