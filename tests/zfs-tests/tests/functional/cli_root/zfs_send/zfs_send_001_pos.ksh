#!/bin/ksh -p
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
# Copyright (c) 2012, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/cli_root/cli_common.kshlib
. $STF_SUITE/tests/functional/cli_root/zfs_send/zfs_send.cfg

#
# DESCRIPTION:
#	Verify 'zfs send' can create valid send streams as expected.
#
# STRATEGY:
#	1. Fill in fs with some data
#	2. Create a full send streams with the fs
#	3. Receive the send stream and verify the data integrity
#	4. Fill in fs with some new data
#	5. Create an incremental send stream with the fs
#	6. Receive the incremental send stream and verify the data integrity.
#

verify_runnable "both"

function cleanup
{
	for snap in $init_snap $inc_snap $rst_snap $rst_inc_snap; do
                snapexists $snap && destroy_dataset $snap -f
        done

	datasetexists $rst_root && destroy_dataset $rst_root -Rf

	for file in $full_bkup $inc_bkup \
			$init_data $inc_data
	do
		[[ -e $file ]] && \
			log_must rm -f $file
	done

	[[ -d $TESTDIR1 ]] && \
		log_must rm -rf $TESTDIR1

}

log_assert "Verify 'zfs send' can create valid send streams as expected."
log_onexit cleanup

init_snap=$TESTPOOL/$TESTFS@init_snap
inc_snap=$TESTPOOL/$TESTFS@inc_snap
full_bkup=$TEST_BASE_DIR/fullbkup.$$
inc_bkup=$TEST_BASE_DIR/incbkup.$$
init_data=$TESTDIR/$TESTFILE1
inc_data=$TESTDIR/$TESTFILE2
orig_sum=""
rst_sum=""
rst_root=$TESTPOOL/rst_ctr
rst_snap=$rst_root/$TESTFS@init_snap
rst_inc_snap=$rst_root/$TESTFS@inc_snap
rst_data=$TESTDIR1/$TESTFS/$TESTFILE1
rst_inc_data=$TESTDIR1/$TESTFS/$TESTFILE2


log_note "Verify 'zfs send' can create full send stream."

#Pre-paration
log_must zfs create $rst_root
[[ ! -d $TESTDIR1 ]] && \
	log_must mkdir -p $TESTDIR1
log_must zfs set mountpoint=$TESTDIR1 $rst_root

file_write -o create -f $init_data -b $BLOCK_SIZE -c $WRITE_COUNT

log_must zfs snapshot $init_snap
log_must eval "zfs send $init_snap > $full_bkup"

log_note "Verify the send stream is valid to receive."

log_must zfs receive $rst_snap <$full_bkup
receive_check $rst_snap ${rst_snap%%@*}
compare_cksum $init_data $rst_data

log_note "Verify 'zfs send -i' can create incremental send stream."

file_write -o create -f $inc_data -b $BLOCK_SIZE -c $WRITE_COUNT -d 0

log_must zfs snapshot $inc_snap
log_must eval "zfs send -i $init_snap $inc_snap > $inc_bkup"

log_note "Verify the incremental send stream is valid to receive."

log_must zfs rollback $rst_snap
log_must zfs receive $rst_inc_snap <$inc_bkup
receive_check $rst_inc_snap
compare_cksum $inc_data $rst_inc_data

log_pass "Verifying 'zfs receive' succeed."
