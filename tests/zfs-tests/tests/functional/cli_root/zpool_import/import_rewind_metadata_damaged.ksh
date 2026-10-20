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
#	2. Record an on-disk root and checkpoint the pool to protect it,
#	   then note the last synced txg before the overwrite.
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
#	The file is small and the txg timeout is disabled to ensure that.
#
#	A single zpool sync advances the pool by several txgs, so the ones
#	between the two below are empty, but they still reference the damaged
#	block and are rejected just like the txg that allocated it.
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

log_must dd if=/dev/urandom of=$file bs=128k count=$blocks conv=fsync
log_must sync_pool $TESTPOOL1
typeset digest=$(xxh128digest $file)

#
# From now on only the explicit syncs below advance the txg, so the pool
# ends up with exactly one txg worth of damage to rewind past.
#
log_must save_tunable TXG_TIMEOUT
log_must set_tunable32 TXG_TIMEOUT 5000

#
# Flush the file and its atime update, and persist an uberblock before
# recording the root.  Empty txgs before checkpoint creation then keep
# that same block tree, including its MOS blocks.
#
log_must sync_pool $TESTPOOL1 true
typeset -i protectedtxg=$(get_last_txg_synced $TESTPOOL1)
log_must test $protectedtxg -gt 0
log_must zpool checkpoint $TESTPOOL1
typeset -i goodtxg=$(get_last_txg_synced $TESTPOOL1)
log_must test $goodtxg -ge $protectedtxg

# This allocates a new indirect block for the file.
log_must dd if=/dev/urandom of=$file bs=128k count=$blocks conv=notrunc,fsync
log_must sync_pool $TESTPOOL1 true
typeset -i badtxg=$(get_last_txg_synced $TESTPOOL1)
log_must test $badtxg -gt $goodtxg
log_note "Protected txg $protectedtxg, last good $goodtxg, damaged $badtxg"

# The old indirect block is protected by the checkpoint, not listed here.
corrupt_blocks_at_level $file 1

log_must zpool export $TESTPOOL1

# The damaged txg can not be loaded, but the one preceding it can.
typeset out
out=$(zpool import -d $DEVICE_DIR -nFX -T $badtxg $TESTPOOL1 2>&1) &&
    log_fail "Loading the damaged txg $badtxg unexpectedly succeeded"
log_note "$out"
# Roots newer than the checkpoint are not protected from block reuse.
# The newest usable root can therefore precede goodtxg, but must not
# precede the protected root or include the damaged write.  The checkpoint's
# own txg need not have an uberblock in the label ring if that txg was empty.
typeset rewindtxg=$(echo "$out" | \
    sed -n 's/.*(txg \([0-9][0-9]*\)).*/\1/p')
[[ $rewindtxg == +([0-9]) ]] || log_fail "No rewind target was reported"
((rewindtxg >= protectedtxg && rewindtxg <= goodtxg)) ||
    log_fail "Rewind target $rewindtxg is outside [$protectedtxg, $goodtxg]"

log_must zpool import -d $DEVICE_DIR -T $badtxg $TESTPOOL1

# The original content is the proof that the pool was actually rewound.
log_must check_pool_healthy $TESTPOOL1
if [[ "$(xxh128digest $file)" != "$digest" ]]; then
	log_fail "The file was not restored to its original content"
fi

# Nothing references the damaged block anymore.
# A root preceding checkpoint creation no longer has a checkpoint to discard.
if [[ "$(get_pool_prop checkpoint $TESTPOOL1)" != "-" ]]; then
	log_must zpool checkpoint -d $TESTPOOL1
fi
log_must zpool scrub -w $TESTPOOL1
log_must check_pool_healthy $TESTPOOL1

log_pass "Rewind past damaged file meta-data."
