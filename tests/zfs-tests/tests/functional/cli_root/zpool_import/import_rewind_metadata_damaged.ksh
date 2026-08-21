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
# Copyright (c) 2026, TrueNAS.
#

. $STF_SUITE/tests/functional/cli_root/zpool_import/zpool_import.kshlib

#
# DESCRIPTION:
#	Damaged file meta-data should make a txg unusable, and it should be
#	possible to rewind the pool to a txg preceding the damage.
#
# STRATEGY:
#	1. Create a pool with a single file and remember its hashsum.
#	2. Checkpoint the pool, so that the blocks of the txgs below are
#	   guaranteed to stay on disk, and note the last synced txg.
#	3. Overwrite the file in a single txg, allocating a new indirect block
#	   for it, and note the last synced txg again.
#	4. Corrupt every copy of that indirect block on disk.
#	5. Verify that a dry run reports the newer txg as unusable and the
#	   older one as a possible rewind target.
#	6. Rewind the pool and verify that the file has its original content.
#
# NOTES:
#	Both imports below request a txg explicitly, which implies an extreme
#	rewind.  A non-extreme import verifies only the blocks born within the
#	last few txgs, so whether it notices the damage would depend on how
#	many txgs the export happens to consume.
#
#	The overwrite has to land in a single txg, otherwise the rewind could
#	stop at an intermediate uberblock holding a partially updated file.
#

verify_runnable "global"

function custom_cleanup
{
	log_pos restore_tunable TXG_TIMEOUT
	cleanup
}

log_onexit custom_cleanup

log_assert "Rewind past damaged file meta-data."

typeset file="/$TESTPOOL1/file"
typeset -i blocks=80

log_must zpool create $TESTPOOL1 $VDEV0

log_must dd if=/dev/urandom of=$file bs=128k count=$blocks
log_must sync_pool $TESTPOOL1
typeset digest=$(xxh128digest $file)

#
# From now on only the explicit syncs below advance the txg, so the pool
# ends up with exactly one txg worth of damage to rewind past.
#
log_must save_tunable TXG_TIMEOUT
log_must set_tunable32 TXG_TIMEOUT 5000

#
# The checkpoint keeps the blocks of the txg we are going to rewind to,
# including the MOS ones, from being reused.
#
log_must zpool checkpoint $TESTPOOL1
typeset -i goodtxg=$(get_last_txg_synced $TESTPOOL1)

# This allocates a new indirect block for the file.
log_must dd if=/dev/urandom of=$file bs=128k count=$blocks conv=notrunc
log_must sync_pool $TESTPOOL1
typeset -i badtxg=$(get_last_txg_synced $TESTPOOL1)

if (( badtxg != goodtxg + 1 )); then
	log_fail "The overwrite did not take exactly one txg" \
	    "($goodtxg -> $badtxg), the rewind target would be ambiguous"
fi

# The old indirect block is protected by the checkpoint, not listed here.
corrupt_blocks_at_level $file 1

log_must zpool export $TESTPOOL1

# The damaged txg can not be loaded, but an older one can.
typeset out
out=$(zpool import -d $DEVICE_DIR -nFX -T $badtxg $TESTPOOL1 2>&1) &&
    log_fail "Loading the damaged txg $badtxg unexpectedly succeeded"
log_note "$out"
echo "$out" | grep -q "Would be able to return" ||
    log_fail "No rewind target reported for the damaged txg $badtxg"

out=$(zpool import -d $DEVICE_DIR -T $badtxg $TESTPOOL1 2>&1) ||
    log_fail "Rewinding the pool failed: $out"
log_note "$out"
echo "$out" | grep -q "returned to its state as of" ||
    log_fail "The pool was imported without a rewind"

log_must check_pool_healthy $TESTPOOL1
if [[ "$(xxh128digest $file)" != "$digest" ]]; then
	log_fail "The file was not restored to its original content"
fi

# Nothing references the damaged block anymore.
log_must zpool checkpoint -d $TESTPOOL1
log_must zpool scrub -w $TESTPOOL1
log_must check_pool_healthy $TESTPOOL1

log_pass "Rewind past damaged file meta-data."
