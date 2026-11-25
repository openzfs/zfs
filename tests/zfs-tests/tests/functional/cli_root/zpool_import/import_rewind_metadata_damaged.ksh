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
#	2. Checkpoint the pool, so that the blocks of the checkpointed state
#	   are guaranteed to stay on disk, and note the last synced txg.
#	3. Overwrite the file in a single txg, allocating a new indirect block
#	   for it, and note the last synced txg again.
#	4. Corrupt every copy of that indirect block on disk.
#	5. Verify that a dry run reports the newer txg as unusable and a txg
#	   from the checkpointed state up to the older one as the rewind
#	   target.
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
#	The file is small and the txg timeout is disabled to ensure that.
#
#	A single zpool sync advances the pool by several txgs, so the ones
#	between the two below are empty, but they still reference the damaged
#	block and are rejected just like the txg that allocated it.
#
#	The checkpoint holds only the blocks born up to its own txg, which is
#	older than the last synced txg: creating the checkpoint syncs further
#	txgs.  The blocks those txgs allocate are freed by the overwrite and
#	can be reused by the txgs the export syncs, so the newest txg before
#	the overwrite may be unloadable, and the rewind then goes on to an
#	older one.  Only the checkpointed state is guaranteed to load, so the
#	target is anywhere from that state up to the newest txg before the
#	overwrite.
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
typeset -i blocks=8

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
# The checkpoint keeps the blocks of the checkpointed state, including
# the MOS ones, from being reused, so the rewind can always reach that
# state.  It is older than the txg noted below; see the NOTES above.
#
log_must zpool checkpoint $TESTPOOL1
# The newest txg before the overwrite.
typeset -i goodtxg=$(get_last_txg_synced $TESTPOOL1)

# This allocates a new indirect block for the file.
log_must dd if=/dev/urandom of=$file bs=128k count=$blocks conv=notrunc
log_must sync_pool $TESTPOOL1
typeset -i badtxg=$(get_last_txg_synced $TESTPOOL1)

# The old indirect block is protected by the checkpoint, not listed here.
corrupt_blocks_at_level $file 1

log_must zpool export $TESTPOOL1

# The oldest txg the rewind can end up at.
typeset -i ckpttxg=$(get_checkpointed_txg $TESTPOOL1 $DEVICE_DIR)
(( ckpttxg > 0 && ckpttxg < goodtxg )) ||
    log_fail "Couldn't read the txg of the checkpointed state"
log_note "checkpointed state: txg $ckpttxg, last synced before the" \
    "overwrite: txg $goodtxg, after it: txg $badtxg"

# The damaged txg can not be loaded, but one preceding it can.
typeset out
out=$(zpool import -d $DEVICE_DIR -nFX -T $badtxg $TESTPOOL1 2>&1) &&
    log_fail "Loading the damaged txg $badtxg unexpectedly succeeded"
log_note "$out"
typeset -i target=$(echo "$out" |
    sed -n 's/.*(txg \([0-9][0-9]*\)).*/\1/p' | head -n 1)
(( target >= ckpttxg && target <= goodtxg )) ||
    log_fail "The rewind target '$target' is not a txg from $ckpttxg to" \
    "$goodtxg"

log_must zpool import -d $DEVICE_DIR -T $badtxg $TESTPOOL1

# The original content is the proof that the pool was actually rewound.
log_must check_pool_healthy $TESTPOOL1
if [[ "$(xxh128digest $file)" != "$digest" ]]; then
	log_fail "The file was not restored to its original content"
fi

# Nothing references the damaged block anymore.  The checkpointed state
# itself predates the checkpoint, so a rewind to it leaves none to discard.
typeset ckpt=$(get_pool_prop checkpoint $TESTPOOL1)
if (( target > ckpttxg )); then
	[[ "$ckpt" != "-" ]] ||
	    log_fail "The checkpoint is gone after a rewind to txg $target"
	log_must zpool checkpoint -d $TESTPOOL1
else
	[[ "$ckpt" == "-" ]] ||
	    log_fail "A rewind to the checkpointed state kept a checkpoint"
fi
log_must zpool scrub -w $TESTPOOL1
log_must check_pool_healthy $TESTPOOL1

log_pass "Rewind past damaged file meta-data."
