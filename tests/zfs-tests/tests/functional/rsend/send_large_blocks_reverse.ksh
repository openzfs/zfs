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
# Copyright (c) 2026 by Martin Minkus. All rights reserved.
#

. $STF_SUITE/tests/functional/rsend/rsend.kshlib

#
# Description:
#   Verifies that once a copy has been replicated without -L, an
#   incremental stream sent back in the opposite direction is rejected,
#   and that passing -L to that send does not help.
#
# Strategy:
#   1. Create a dataset with a 1MB recordsize and write a large file,
#      which activates the large_blocks feature.
#   2. Replicate it without -L, so the large blocks are split and the
#      copy does not activate the feature.
#   3. Modify the copy and send an incremental stream back with -L.
#      The stream cannot carry large blocks, because the sending copy
#      has none, so the receive must fail.
#   4. Confirm the documented remedy works: a full receive of the copy
#      into a new dataset succeeds.
#

verify_runnable "both"

log_assert "A reverse incremental send is rejected when the sending copy" \
    "has no large blocks"

typeset srcfs=$POOL/src
typeset destfs=$POOL2/dest
typeset newfs=$POOL/restored
typeset errfile=$TEST_BASE_DIR/send_large_blocks_reverse.$$

function cleanup
{
	rm -f $errfile
	cleanup_pool $POOL
	cleanup_pool $POOL2
}
log_onexit cleanup

function assert_feature_state
{
	typeset pool=$1
	typeset expected_state=$2

	typeset actual_state=$(zpool get -H -o value feature@large_blocks $pool)
	log_note "Zpool $pool feature@large_blocks=$actual_state"
	if [[ "$actual_state" != "$expected_state" ]]; then
		log_fail "pool $pool feature@large_blocks=$actual_state" \
		    "(expected '$expected_state')"
	fi
}

# The source holds real large blocks, so the feature is active.
log_must zfs create -o recordsize=1M $srcfs
typeset srcmnt=$(get_prop mountpoint $srcfs)
log_must dd if=/dev/urandom of=$srcmnt/big.bin bs=1M count=4
log_must zpool sync $POOL
log_must zfs snapshot $srcfs@a

assert_feature_state $POOL "active"
assert_feature_state $POOL2 "enabled"

# Replicating without -L splits the large blocks, so the copy never
# activates the feature.
log_must eval "zfs send $srcfs@a | zfs receive $destfs"
assert_feature_state $POOL2 "enabled"

# Now reverse the direction.  -L cannot be honoured here: the sending
# copy has no large blocks, so the stream does not carry them, and the
# original still does.
typeset destmnt=$(get_prop mountpoint $destfs)
log_must dd if=/dev/urandom of=$destmnt/small.bin bs=4k count=1
log_must zpool sync $POOL2
log_must zfs snapshot $destfs@b

log_mustnot eval "zfs send -L -i @a $destfs@b |" \
    "zfs receive -F $srcfs 2>$errfile"
log_must grep -q "large blocks" $errfile

# Sending the same stream without -L fails the same way; the flag is
# not what is missing.
log_mustnot eval "zfs send -i @a $destfs@b | zfs receive -F $srcfs"

# The documented remedy: receive a full stream into a new dataset.
log_must eval "zfs send $destfs@b | zfs receive $newfs"

log_pass "A reverse incremental send is rejected when the sending copy" \
    "has no large blocks"
