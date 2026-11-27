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

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_import/zpool_import.kshlib

#
# DESCRIPTION:
# A failed import attempt of a pool with cloned and deduplicated blocks
# must leave nothing behind that the retry uses (openzfs/zfs#18024).
#
# STRATEGY:
# 1. Create a pool of several vdevs with cloned and deduplicated blocks,
#    and checkpoint it.
# 2. Overwrite a file, allocating a new indirect block for it, freeze
#    the pool and damage that block.
# 3. Import the pool at its newest txg.  That load gets past the BRT
#    and fails its verification, and the load is retried at older txgs.
# 4. Verify that the retry happened after a load that loaded the BRT,
#    that the pool was rewound past the damage, and that its file
#    contents and its block cloning and dedup accounting are intact.
#
# NOTES:
#	The pool is frozen right after the overwrite, so nothing is written
#	to it after the txgs the freeze waits for.  The newest txg on disk
#	is then intact except for the damaged block, and its load always
#	fails in spa_load_verify(), after brt_load().  An export instead
#	syncs many more txgs, which can reuse blocks the newest txgs still
#	reference, and the load then fails before it reaches the BRT.
#
#	The checkpoint keeps the blocks of the checkpointed state from
#	being reused, so a rewind can always reach that state.  The newest
#	txg before the overwrite usually loads too, but the blocks the
#	overwrite freed can be reused by the txgs the freeze waits for, so
#	the rewind target is anywhere from the checkpointed state up to it.
#

verify_runnable "global"

typeset newpool="${TESTPOOL1}_retry_$$"

function custom_cleanup
{
	poolexists $newpool && destroy_pool $newpool
	log_pos restore_tunable TXG_TIMEOUT
	log_pos restore_tunable DBGMSG_ENABLE
	cleanup
}

#
# Count the dbgmsg lines of the loads of $newpool matching a pattern.
#
function count_load_msgs # pattern
{
	kstat dbgmsg | grep -c "spa_load($newpool, .*$1"
}

log_onexit custom_cleanup

log_assert "A retried import does not use the BRT state of the failed attempt"

typeset file="/$TESTPOOL1/file"
typeset -i blocks=8

log_must save_tunable DBGMSG_ENABLE
log_must set_tunable32 DBGMSG_ENABLE 1

log_must zpool create -o feature@block_cloning=enabled $TESTPOOL1 \
    $VDEV0 $VDEV1 $VDEV2 $VDEV3 $VDEV4

#
# Cloned blocks give the pool a BRT; deduplicated ones make each load
# recompute the pool's space in vdev_load(), before it loads the BRT.
#
log_must dd if=/dev/urandom of=/$TESTPOOL1/orig bs=128k count=$blocks
log_must clonefile -f /$TESTPOOL1/orig /$TESTPOOL1/clone
log_must zfs create -o dedup=on $TESTPOOL1/dedup
log_must dd if=/dev/urandom of=/$TESTPOOL1/dedup/a bs=128k count=$blocks
log_must cp /$TESTPOOL1/dedup/a /$TESTPOOL1/dedup/b

log_must dd if=/dev/urandom of=$file bs=128k count=$blocks
log_must sync_pool $TESTPOOL1
typeset digest=$(xxh128digest $file)
typeset bcloneused=$(get_pool_prop bcloneused $TESTPOOL1)
typeset bclonesaved=$(get_pool_prop bclonesaved $TESTPOOL1)
typeset dedupsaved=$(get_pool_prop dedupsaved $TESTPOOL1)
(( bclonesaved > 0 && dedupsaved > 0 )) ||
    log_fail "No cloned ($bclonesaved) or deduplicated ($dedupsaved) blocks"

#
# From now on only the explicit syncs below advance the txg, so the
# overwrite lands in a single txg.
#
log_must save_tunable TXG_TIMEOUT
log_must set_tunable32 TXG_TIMEOUT 5000

log_must zpool checkpoint $TESTPOOL1
# The newest txg before the overwrite.
typeset -i goodtxg=$(get_last_txg_synced $TESTPOOL1)

# This allocates a new indirect block for the file.
log_must dd if=/dev/urandom of=$file bs=128k count=$blocks conv=notrunc
log_must zpool freeze $TESTPOOL1

# The old indirect block is protected by the checkpoint, not listed here.
corrupt_blocks_at_level $file 1

log_must zpool export $TESTPOOL1

# The newest txg on disk, which references the damaged block.
typeset -i badtxg=$(zdb -e -p $DEVICE_DIR -u $TESTPOOL1 |
    awk '$1 == "txg" { print $3; exit }')
# The oldest txg the rewind can end up at.
typeset -i ckpttxg=$(get_checkpointed_txg $TESTPOOL1 $DEVICE_DIR)
(( ckpttxg > 0 && ckpttxg < goodtxg && goodtxg < badtxg )) ||
    log_fail "Unexpected txgs: checkpointed state $ckpttxg, before the" \
    "overwrite $goodtxg, newest $badtxg"
log_note "checkpointed state: txg $ckpttxg, last synced before the" \
    "overwrite: txg $goodtxg, newest: txg $badtxg"

#
# Loading an explicit txg verifies the whole pool.  The pool is imported
# under a new name, so the dbgmsg lines of its loads are its own.  The
# frozen pool's labels were not updated by the export, hence -f.
#
typeset -i verify0=$(count_load_msgs "FAILED: spa_load_verify failed")
typeset -i retry0=$(count_load_msgs "spa_load_retry: rewind")
typeset out
out=$(zpool import -f -d $DEVICE_DIR -T $badtxg $TESTPOOL1 $newpool 2>&1) ||
    log_fail "zpool import -T $badtxg failed: $out"
log_note "$out"
typeset -i verify1=$(count_load_msgs "FAILED: spa_load_verify failed")
typeset -i retry1=$(count_load_msgs "spa_load_retry: rewind")

#
# A load fails its verification only after it loaded the BRT, so a retry
# after such a failure is the case this test is about.
#
(( verify1 > verify0 )) ||
    log_fail "No load of txg $badtxg failed its verification"
(( retry1 > retry0 )) || log_fail "The failed load was not retried"
log_note "$((verify1 - verify0)) loads failed their verification," \
    "$((retry1 - retry0)) retries"

typeset -i target=$(echo "$out" |
    sed -n 's/.*(txg \([0-9][0-9]*\)).*/\1/p' | head -n 1)
(( target >= ckpttxg && target <= goodtxg )) ||
    log_fail "The rewind target '$target' is not a txg from $ckpttxg to" \
    "$goodtxg"

log_must check_pool_healthy $newpool
if [[ "$(xxh128digest /$newpool/file)" != "$digest" ]]; then
	log_fail "The file was not restored to its original content"
fi

#
# The clones and the deduplicated copies predate the checkpoint, so every
# txg the rewind can end up at has the same of them.
#
typeset prop
for prop in bcloneused bclonesaved dedupsaved; do
	typeset expect=$(eval echo \$$prop) val=$(get_pool_prop $prop $newpool)
	[[ "$val" == "$expect" ]] ||
	    log_fail "$prop is $val after the rewind, $expect before"
done
log_must cmp /$newpool/orig /$newpool/clone
log_must cmp /$newpool/dedup/a /$newpool/dedup/b

# Nothing references the damaged block anymore.  The checkpointed state
# itself predates the checkpoint, so a rewind to it leaves none to discard.
typeset ckpt=$(get_pool_prop checkpoint $newpool)
if (( target > ckpttxg )); then
	[[ "$ckpt" != "-" ]] ||
	    log_fail "The checkpoint is gone after a rewind to txg $target"
	log_must zpool checkpoint -d -w $newpool
else
	[[ "$ckpt" == "-" ]] ||
	    log_fail "A rewind to the checkpointed state kept a checkpoint"
fi
log_must zpool scrub -w $newpool
log_must check_pool_healthy $newpool

log_pass "A retried import does not use the BRT state of the failed attempt"
