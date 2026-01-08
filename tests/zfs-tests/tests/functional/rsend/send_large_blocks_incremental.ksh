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
#   Verifies that an incremental receive activates the large block feature when the stream was sent
#   from a dataset whose large block feature was activated.
#
# Strategy:
#   1. Create a dataset with 1MB recordsize.
#   2. Create a snapshot at where the feature is inactive.
#   3. Create and delete a large file to activate the large_blocks feature.
#   4. Create a snapshot where the feature is active.
#   5. Send the initial snapshot to a second dataset, where the large_blocks feature remains inactive.
#   6. Send the second snapshot to the dataset incrementally, which should activate the large_blocks feature.
#

verify_runnable "both"

log_assert "Verify incremental receive handles inactive large_blocks feature correctly."

function cleanup
{
	cleanup_pool $POOL
	cleanup_pool $POOL2
}
log_onexit cleanup

function assert_feature_state {
	typeset pool=$1
	typeset expected_state=$2

	typeset actual_state=$(zpool get -H -o value feature@large_blocks $pool)
	log_note "Zpool $pool feature@large_blocks=$actual_state"
	if [[ "$actual_state" != "$expected_state" ]]; then
		log_fail "pool $pool feature@large_blocks=$actual_state (expected '$expected_state')"
	fi
}

typeset srcfs=$POOL/src
typeset destfs=$POOL2/dest

# Create a dataset with a large recordsize (1MB) where the feature is inactive in the initial snapshot
# but active in a later snapshots.
log_must zfs create -o recordsize=1M $srcfs
typeset mntpnt=$(get_prop mountpoint $srcfs)
log_must zfs snapshot $srcfs@feature-inactive
log_must dd if=/dev/urandom of=$mntpnt/big.bin bs=1M count=1
log_must zpool sync $POOL
log_must rm $mntpnt/big.bin
log_must zfs snapshot $srcfs@feature-active

# Assert initial state of pools
assert_feature_state $POOL "active"
assert_feature_state $POOL2 "enabled"

# Initial send does not activate feature.
log_must eval "zfs send -p -L $srcfs@feature-inactive | zfs receive $destfs"
assert_feature_state $POOL2 "enabled"

# Incremental send activates feature.
log_must eval "zfs send -L -i $srcfs@feature-inactive $srcfs@feature-active | zfs receive $destfs"
assert_feature_state $POOL2 "active"

log_pass "Feature activation propagated successfully."
