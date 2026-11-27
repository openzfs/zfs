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
. $STF_SUITE/tests/functional/block_cloning/block_cloning.kshlib

#
# DESCRIPTION:
#	Concurrent partial writes into blocks that were cloned in the same
#	transaction group must not crash, must all land in the clone, must
#	leave the source untouched and must drop the pending clones.
#
#	The first partial write into a block cloned in this txg reads the
#	cloned data and turns the clone's dirty record into a regular one.
#	When that dropped db_mtx between undirtying the clone and dirtying
#	the block again, other writers into the same block could undirty
#	the new dirty record (corrupting the parent's list of dirty records)
#	or see cached data that is not dirty over a hole (dbuf_verify()).
#
# STRATEGY:
#	1. Create a source file of 64 random 128K blocks and sync it.
#	2. Clone it with a long txg timeout, so the clone and the writes
#	   share a txg, and with the source evicted, so that every block's
#	   first writer reads the cloned data from disk while the others
#	   wait for it.
#	3. 32 fio jobs each overwrite their own 4K of every block, so all
#	   32 write into each block at the same time and together overwrite
#	   the whole file.
#	4. Verify the clone holds only the written pattern and the source is
#	   unchanged, from the cache and after an export/import.
#	5. Verify no cloned block is left (bcloneused is 0) and that a scrub
#	   finds no errors.
#

verify_runnable "global"

if ! command -v clonefile > /dev/null ; then
	log_unsupported "clonefile program required to test block cloning"
fi

claim="Concurrent writes into blocks cloned in the same txg are consistent"

typeset SRC=/$TESTPOOL/source
typeset DST=/$TESTPOOL/clone
typeset -i NBLOCKS=64
typeset -i NJOBS=32
typeset -i ROUNDS=10

function cleanup
{
	zinject -c all > /dev/null 2>&1
	datasetexists $TESTPOOL && destroy_pool $TESTPOOL
	restore_tunable TXG_TIMEOUT
}

log_onexit cleanup

log_must save_tunable TXG_TIMEOUT
log_must zpool create -o feature@block_cloning=enabled \
    -O recordsize=128k -O compression=off $TESTPOOL $DISKS
typeset disk=$(get_disklist $TESTPOOL | awk '{print $1}')

log_must dd if=/dev/urandom of=$SRC bs=128k count=$NBLOCKS
log_must sync_pool $TESTPOOL
typeset src_hash=$(xxh128digest $SRC)

# Every 4K of the file is overwritten with 'Z' (0x5a) by exactly one job.
typeset expect=$TEST_BASE_DIR/expect.$$
dd if=/dev/zero bs=128k count=$NBLOCKS 2>/dev/null | tr '\000' Z > $expect
log_must test "$(wc -c < $expect)" -eq $((NBLOCKS * 128 * 1024))
typeset expect_hash=$(xxh128digest $expect)
log_must rm -f $expect

# The platform clones rather than copies.
log_must clonefile -f $SRC $DST
log_must sync_pool $TESTPOOL
log_must test "$(get_pool_prop bcloneused $TESTPOOL)" = "$((NBLOCKS * 128 * 1024))"
log_must rm -f $DST
log_must sync_pool $TESTPOOL
log_must test "$(get_pool_prop bcloneused $TESTPOOL)" = "0"

log_must set_tunable64 TXG_TIMEOUT 60

for round in $(seq 1 $ROUNDS); do
	log_must zpool export $TESTPOOL
	log_must zpool import $TESTPOOL
	log_must zpool sync $TESTPOOL

	log_must clonefile -f $SRC $DST
	log_must zinject -d $disk -D 10:1 -T read $TESTPOOL
	log_must fio --name=clone-write --filename=$DST --thread \
	    --ioengine=psync --rw=write:124k --bs=4k --numjobs=$NJOBS \
	    --offset_increment=4k --size=$((NBLOCKS * 128 - 124))k \
	    --buffer_pattern=0x5a --group_reporting --minimal
	log_must zinject -c all

	log_must test "$(xxh128digest $DST)" = "$expect_hash"
	log_must test "$(xxh128digest $SRC)" = "$src_hash"
	log_must sync_pool $TESTPOOL
	log_must test "$(get_pool_prop bcloneused $TESTPOOL)" = "0"
	log_must rm -f $DST
	log_note "round $round of $ROUNDS done"
done

log_must clonefile -f $SRC $DST
log_must fio --name=clone-write --filename=$DST --thread \
    --ioengine=psync --rw=write:124k --bs=4k --numjobs=$NJOBS \
    --offset_increment=4k --size=$((NBLOCKS * 128 - 124))k \
    --buffer_pattern=0x5a --group_reporting --minimal
log_must zpool export $TESTPOOL
log_must zpool import $TESTPOOL
log_must test "$(xxh128digest $DST)" = "$expect_hash"
log_must test "$(xxh128digest $SRC)" = "$src_hash"
log_must test "$(get_pool_prop bcloneused $TESTPOOL)" = "0"

log_must zpool scrub -w $TESTPOOL
log_must check_pool_status $TESTPOOL "errors" "No known data errors"
log_must is_pool_scrubbed $TESTPOOL

log_pass $claim
