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
# Copyright 2026 Oxide Computer Company
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/block_cloning/block_cloning.kshlib

#
# DESCRIPTION:
#	A write that is logged indirect, to a block that a clone then
#	replaces in the same txg, must not cut the log short.
#
#	When the log block is written, the indirect write's dmu_sync() finds
#	the block overridden by the clone.  The clone's blkptr points to a
#	block allocated in an earlier txg, which the log can't claim, so
#	logging it would end the log at that record when the pool is
#	imported, losing later records.
#
# STRATEGY:
#	1. Stop txgs from syncing on their own
#	2. Write block 0 of a file, logged indirect, without fsync; clone
#	   another file's block over it; then write block 8 and fsync
#	3. Save the vdev, which then holds the log but not the writes, as
#	   after a crash
#	4. Restore the saved vdev and import the pool to replay the log
#	5. Check that block 8 has its write and block 0 has the clone
#

verify_runnable "global"

export VDIR=$TEST_BASE_DIR/disk-bclone-ri

claim="A clone over an indirect write keeps later log records."

log_assert $claim

function cleanup
{
	restore_tunable TXG_TIMEOUT
	datasetexists $TESTPOOL && destroy_pool $TESTPOOL
	rm -rf $VDIR
}

log_onexit cleanup

log_must rm -rf $VDIR
log_must mkdir -p $VDIR
log_must truncate -s $MINVDEVSIZE $VDIR/a
log_must save_tunable TXG_TIMEOUT

log_must zpool create -o feature@block_cloning=enabled $TESTPOOL $VDIR/a
log_must zfs create -o compression=off -o recordsize=128k \
    -o logbias=throughput $TESTPOOL/$TESTFS

typeset dir=/$TESTPOOL/$TESTFS
log_must dd if=/dev/urandom of=$dir/src bs=128k count=1
log_must dd if=/dev/zero of=$dir/dst bs=128k count=16
log_must dd if=/dev/urandom of=$VDIR/w bs=128k count=1
log_must dd if=/dev/urandom of=$VDIR/w2 bs=128k count=1

# Create the ZIL header now, so that the fsync below doesn't sync a txg.
log_must dd if=/dev/zero of=$dir/sync oflag=sync bs=1 count=1
log_must sync_pool $TESTPOOL

#
# 1. Stop txgs from syncing on their own.  The sync thread picks up the new
#    timeout after the next txg.
#
log_must set_tunable32 TXG_TIMEOUT 3600
log_must sync_pool $TESTPOOL

#
# 2. Write block 0, clone over it, then write block 8 and fsync.
#
log_must dd if=$VDIR/w of=$dir/dst bs=128k count=1 conv=notrunc
log_must clonefile -f $dir/src $dir/dst 0 0 131072
log_must dd if=$VDIR/w2 of=$dir/dst bs=128k count=1 seek=8 \
    conv=notrunc,fsync

#
# 3. Save the vdev, then let the pool go.
#
log_must cp $VDIR/a $VDIR/a.save
log_must restore_tunable TXG_TIMEOUT
log_must zpool export $TESTPOOL

#
# 4. Restore the saved vdev and replay the log.
#
log_must cp $VDIR/a.save $VDIR/a
typeset recs=$(zdb -e -p $VDIR -iv $TESTPOOL/$TESTFS)
echo "$recs"
echo "$recs" | grep -q "TX_CLONE_RANGE" || \
    log_fail "the log has no TX_CLONE_RANGE"

log_must zpool import -d $VDIR $TESTPOOL

#
# 5. Check both blocks.
#
log_must dd if=$dir/dst of=$VDIR/b8 bs=128k count=1 skip=8
log_must cmp $VDIR/b8 $VDIR/w2
log_must dd if=$dir/dst of=$VDIR/b0 bs=128k count=1
log_must cmp $VDIR/b0 $dir/src

log_pass $claim
