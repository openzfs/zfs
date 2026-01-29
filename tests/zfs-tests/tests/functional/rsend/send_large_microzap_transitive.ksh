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
# http://www.illumos.org/license/CDDL.
#

#
# Copyright (c) 2026 by Austin Wise. All rights reserved.
#

. $STF_SUITE/tests/functional/rsend/rsend.kshlib
. $STF_SUITE/include/properties.shlib

#
# Description:
#   Ensures that sending a snapshot with the large microzap feature active propagates
#   the feature activation to the receiving pool. Also checks that the received snapshot also
#   correctly propagates the feature activation when sent to a third third pool.
#	Regression test for https://github.com/openzfs/zfs/issues/18143
#
# Strategy:
#   1. Enable the large microzap feature in the source dataset.
#   2. Create a snapshot.
#   3. Send the snapshot from the first pool to a second pool.
#   4. Send the snapshot from the second pool to a third pool.
#   5. Verify that all pools have the large microzap feature active.
#

verify_runnable "both"
verify_disk_count "$DISKS" 3

log_assert "Verify incremental receive handles inactive large_blocks feature correctly."

function cleanup
{
	restore_tunable ZAP_MICRO_MAX_SIZE
	cleanup_pool $POOL
	cleanup_pool $POOL2
	cleanup_pool $POOL3
}

function assert_feature_state {
	typeset pool=$1
	typeset expected_state=$2

	typeset actual_state=$(zpool get -H -o value feature@large_microzap $pool)
	log_note "Zpool $pool feature@large_microzap=$actual_state"
	if [[ "$actual_state" != "$expected_state" ]]; then
		log_fail "pool $pool feature@large_microzap=$actual_state (expected '$expected_state')"
	fi
}

typeset src=$POOL/src
typeset second=$POOL2/second
typeset third=$POOL3/third

log_onexit cleanup

# Allow micro ZAPs to grow beyond SPA_OLD_MAXBLOCKSIZE.
set_tunable64 ZAP_MICRO_MAX_SIZE 1048576

# Ensure the third pool exists.
datasetexists $POOL3 || log_must zpool create $POOL3 $DISK3

# Create a dataset with a large recordsize (1MB)
log_must zfs create -o recordsize=1M $src
typeset mntpnt=$(get_prop mountpoint $src)

# Activate the large_microzap feature by creating a micro ZAP that is larger than SPA_OLD_MAXBLOCKSIZE (128k)
# but smaller than MZAP_MAX_SIZE (1MB). Each micro ZAP entry is 64 bytes (MZAP_ENT_LEN),
# so 4096 files is about 256k.
log_must eval "seq 1 4096 | xargs -I REPLACE_ME touch $mntpnt/REPLACE_ME"
log_must zpool sync $POOL

# Assert initial state of pools
assert_feature_state $POOL "active"
assert_feature_state $POOL2 "enabled"
assert_feature_state $POOL3 "enabled"

# Create initial snapshot and send to second pool.
log_must zfs snapshot $src@snap
log_must eval "zfs send -p -L $src@snap | zfs receive $second"
log_must zpool sync $POOL2
assert_feature_state $POOL2 "active"

# Send to third pool from second. This ensures that the second pool correctly recorded that that
# large_microzap feature was active when it received the first send stream.
log_must eval "zfs send -p -L $second@snap | zfs receive $third"
log_must zpool sync $POOL3
assert_feature_state $POOL3 "active"

log_pass "Feature activation propagated successfully."
