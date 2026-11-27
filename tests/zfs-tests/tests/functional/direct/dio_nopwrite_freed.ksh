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
. $STF_SUITE/tests/functional/direct/dio.kshlib

#
# DESCRIPTION:
# A Direct I/O write must not nopwrite against a block that was freed
# in an earlier TXG that has not synced yet.
#
# The free turns the block pointer into a hole when that TXG syncs, so
# the nopwrite override no longer matches the on-disk block pointer.
# Syncing the Direct I/O write then fails
# VERIFY(BP_EQUAL(zio->io_bp, &zio->io_bp_orig)) in zio_done(); without
# that check the file would reference the freed block.
#
# STRATEGY:
# 1. Create a pool with nopwrite enabled (sha256, compression).
# 2. Write a file and sync it.
# 3. Keep the next TXG from syncing: dirty some data, delay the vdev
#    and start syncing it.
# 4. Truncate the file in the next TXG, then quiesce that TXG with a
#    long free that exceeds zfs_per_txg_dirty_frees_percent of
#    zfs_dirty_data_max, which is pinned so that this does not depend
#    on the amount of memory.  Check that the long free did wait.
# 5. Rewrite the first block of the file with the same data with Direct
#    I/O, in the TXG after it.
# 6. Remove the delay, sync, and verify the file and the pool.
#

verify_runnable "global"

TESTPOOL2=testpool_dio_nopwrite
VDEV=$TEST_BASE_DIR/vdev_dio_nopwrite
BLK=$TEST_BASE_DIR/dio_nopwrite_blk

function cleanup
{
	zinject -c all >/dev/null 2>&1
	restore_tunable TXG_TIMEOUT
	restore_tunable PER_TXG_DIRTY_FREES_PERCENT
	restore_tunable DIRTY_DATA_MAX
	destroy_pool $TESTPOOL2
	rm -f $VDEV $BLK
}

log_assert "Direct I/O does not nopwrite against a pending free"
log_onexit cleanup

log_must save_tunable TXG_TIMEOUT
log_must save_tunable PER_TXG_DIRTY_FREES_PERCENT
log_must save_tunable DIRTY_DATA_MAX
log_must set_tunable64 TXG_TIMEOUT 600

log_must rm -f $VDEV
log_must truncate -s 2G $VDEV
log_must zpool create -O checksum=sha256 -O compression=lz4 \
    -O recordsize=128k -O atime=off $TESTPOOL2 $VDEV
log_must zfs create -o direct=standard $TESTPOOL2/fs
mntpnt=$(get_prop mountpoint $TESTPOOL2/fs)
file=$mntpnt/file

log_must stride_dd -i /dev/urandom -o $BLK -b 131072 -c 1
log_must cp $BLK $file
log_must stride_dd -i /dev/urandom -o $file -b 131072 -c 3 -k 1
log_must zpool sync $TESTPOOL2

# A sparse file with 40 L1 blocks (5 MiB of them), whose truncation is
# a long free of more than 1% of zfs_dirty_data_max (256 MiB), and
# another file to free after it.
for i in $(seq 0 39); do
	log_must dd if=/dev/urandom of=$mntpnt/sparse bs=131072 count=1 \
	    seek=$((i * 1024)) conv=notrunc
done
log_must stride_dd -i /dev/urandom -o $mntpnt/other -b 131072 -c 8
log_must zpool sync $TESTPOOL2
log_must set_tunable64 DIRTY_DATA_MAX $((256 * 1024 * 1024))
log_must set_tunable32 PER_TXG_DIRTY_FREES_PERCENT 1

# Dirty data to sync slowly, and start syncing it.
log_must zinject -d $VDEV -D 400:1 -T write $TESTPOOL2
log_must stride_dd -i /dev/urandom -o $mntpnt/filler -b 1048576 -c 32
zpool sync $TESTPOOL2 &
sync1=$!
log_must sleep 2

# Free the first block in the next TXG.  The third truncation waits
# for that TXG to quiesce (dmu_tx_dirty_frees_delay), and the TXG then
# stays quiesced until the slow sync is done.
typeset -i frees_delay=$(kstat dmu_tx.dmu_tx_dirty_frees_delay)
log_must truncate -s 0 $file
log_must truncate -s 0 $mntpnt/sparse
truncate -s 0 $mntpnt/other &
trunc=$!
log_must sleep 2
if (( $(kstat dmu_tx.dmu_tx_dirty_frees_delay) <= frees_delay )); then
	log_fail "The long free did not wait for the TXG to quiesce"
fi

# Rewrite the block with the same data with Direct I/O.
prev_dio_wr=$(kstat_pool $TESTPOOL2 iostats.direct_write_count)
log_must stride_dd -i $BLK -o $file -b 131072 -c 1 -D
curr_dio_wr=$(kstat_pool $TESTPOOL2 iostats.direct_write_count)
if [[ $curr_dio_wr -le $prev_dio_wr ]]; then
	log_fail "The rewrite was not a Direct I/O write"
fi

log_must zinject -c all
log_must wait $sync1
log_must wait $trunc
log_must zpool sync $TESTPOOL2
log_must zpool sync $TESTPOOL2

log_must cmp $BLK $file
log_must zpool scrub -w $TESTPOOL2
log_must check_pool_status $TESTPOOL2 "errors" "No known data errors"

log_pass "Direct I/O does not nopwrite against a pending free"
