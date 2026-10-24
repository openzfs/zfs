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
# 1. Create a pool of several vdevs with cloned and deduplicated blocks.
# 2. Damage an indirect block written in the last txg, so the first load
#    of the pool fails its verification and is retried with a rewind.
# 3. Import it at that txg: the first load fails and is retried at the
#    txg before, which must succeed and leave the pool healthy.
#

verify_runnable "global"

function custom_cleanup
{
	log_pos restore_tunable TXG_TIMEOUT
	cleanup
}

log_onexit custom_cleanup

log_assert "A retried import does not use the BRT state of the failed attempt"

typeset file="/$TESTPOOL1/file"
typeset -i blocks=8

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

#
# From now on only the explicit syncs below advance the txg, so the pool
# ends up with exactly one txg worth of damage to rewind past.
#
log_must save_tunable TXG_TIMEOUT
log_must set_tunable32 TXG_TIMEOUT 5000

# The checkpoint keeps the blocks of the txg we are going to rewind to.
log_must zpool checkpoint $TESTPOOL1

# This allocates a new indirect block for the file.
log_must dd if=/dev/urandom of=$file bs=128k count=$blocks conv=notrunc
log_must sync_pool $TESTPOOL1
typeset -i badtxg=$(get_last_txg_synced $TESTPOOL1)

corrupt_blocks_at_level $file 1

log_must zpool export $TESTPOOL1

#
# Loading an explicit txg verifies the whole pool, so the first load fails
# in spa_load_verify() and is retried at the txg before the damage.
#
log_must zpool import -d $DEVICE_DIR -T $badtxg $TESTPOOL1

# The original content is the proof that the load was retried.
log_must check_pool_healthy $TESTPOOL1
if [[ "$(xxh128digest $file)" != "$digest" ]]; then
	log_fail "The file was not restored to its original content"
fi
log_must zpool checkpoint -d $TESTPOOL1
log_must zpool scrub -w $TESTPOOL1
log_must check_pool_healthy $TESTPOOL1

log_pass "A retried import does not use the BRT state of the failed attempt"
