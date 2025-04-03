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
# Copyright (c) 2020 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION: Identify the objset id and the object id of a file in a
# filesystem, and verify that zfs_ids_to_path behaves correctly with them.
#
# STRATEGY:
# 1. Create a dataset
# 2. Makes files in the dataset
# 3. Verify that zfs_ids_to_path outputs the correct format for each one
#

verify_runnable "both"

function cleanup
{
	destroy_dataset $TESTPOOL/$TESTFS
	zfs create -o mountpoint=$TESTDIR $TESTPOOL/$TESTFS
}

function test_one
{
	typeset ds_id="$1"
	typeset ds_path="$2"
	typeset file_path="$3"

	typeset mntpnt=$(get_prop mountpoint $ds_path)
	typeset file_id=$(ls -i /$mntpnt/$file_path | sed 's/ .*//')
	typeset output=$(zfs_ids_to_path $TESTPOOL $ds_id $file_id)
	[[ "$output" == "$mntpnt/$file_path" ]] || \
		log_fail "Incorrect output for non-verbose while mounted: $output"
	output=$(zfs_ids_to_path -v $TESTPOOL $ds_id $file_id)
	[[ "$output" == "$ds_path:/$file_path" ]] || \
		log_fail "Incorrect output for verbose while mounted: $output"
	log_must zfs unmount $ds_path
	output=$(zfs_ids_to_path $TESTPOOL $ds_id $file_id)
	[[ "$output" == "$ds_path:/$file_path" ]] || \
		log_fail "Incorrect output for non-verbose while unmounted: $output"
	output=$(zfs_ids_to_path -v $TESTPOOL $ds_id $file_id)
	[[ "$output" == "$ds_path:/$file_path" ]] || \
		log_fail "Incorrect output for verbose while unmounted: $output"
	log_must zfs mount $ds_path
}

log_onexit cleanup

typeset BASE=$TESTPOOL/$TESTFS
typeset TESTFILE1=f1
typeset TESTDIR1=d1
typeset TESTFILE2=d1/f2
typeset TESTDIR2=d1/d2
typeset TESTFILE3=d1/d2/f3
typeset TESTFILE4=d1/d2/f4

typeset mntpnt=$(get_prop mountpoint $BASE)

log_must touch /$mntpnt/$TESTFILE1
log_must mkdir /$mntpnt/$TESTDIR1
log_must touch /$mntpnt/$TESTFILE2
log_must mkdir /$mntpnt/$TESTDIR2
log_must touch /$mntpnt/$TESTFILE3
log_must touch /$mntpnt/$TESTFILE4

typeset ds_id=$(zdb $BASE | grep "^Dataset" | sed 's/.* ID \([0-9]*\).*/\1/')
test_one $ds_id $BASE $TESTFILE1
test_one $ds_id $BASE $TESTFILE2
test_one $ds_id $BASE $TESTFILE3
test_one $ds_id $BASE $TESTFILE4

log_pass "zfs_ids_to_path displayed correctly"
