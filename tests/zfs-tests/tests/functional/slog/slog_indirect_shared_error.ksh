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

. $STF_SUITE/tests/functional/slog/slog.kshlib
. $STF_SUITE/include/kstat.shlib

#
# DESCRIPTION:
#	When an indirect write's block fails to be written, the log records
#	that share that block pointer must fail too, and fsync must fall back
#	to waiting for the txg to sync.
#
#	A second indirect write to a block whose dmu_sync() write is in
#	progress waits for that write and shares its block pointer.  If the
#	write fails, the waiting record's log block must fail with it.
#
# STRATEGY:
#	1. Stop txgs from syncing on their own
#	2. For each of NREC records, write its first 64K (W1); then for each
#	   record, write 32K at 64K (W2)
#	3. Delay writes to the data vdev, so that each W2's dmu_sync() waits
#	   for W1's, and make them fail
#	4. fsync, which must see the log write fail, fall back to a txg sync,
#	   and block when the pool suspends (failmode=wait)
#	5. Stop the errors and resume the pool; the fsync must succeed
#	6. Check the file, after an export and import
#

verify_runnable "global"

function cleanup_fs
{
	zinject -c all
	zpool clear $TESTPOOL
	restore_tunable TXG_TIMEOUT
	cleanup
}

log_assert "A failed indirect write fails the log records that share it."
log_onexit cleanup_fs
log_must setup
log_must save_tunable TXG_TIMEOUT

typeset file=/$TESTPOOL/$TESTFS/file
typeset -i NREC=16
typeset -i i
set -A vdevs $VDIR/big $VDIR/e

log_must truncate -s 1g ${vdevs[0]}
log_must zpool create -o failmode=wait $TESTPOOL ${vdevs[0]} log ${vdevs[1]}
log_must zfs create -o compression=off -o recordsize=128k \
    -o logbias=throughput $TESTPOOL/$TESTFS

log_must mkdir -p $TESTDIR
log_must dd if=/dev/urandom of=$TESTDIR/w1 bs=64k count=1
log_must dd if=/dev/urandom of=$TESTDIR/w2 bs=32k count=1
log_must dd if=/dev/zero of=$file bs=128k count=$NREC

# Create the ZIL header now, so that the fsync below doesn't sync a txg.
log_must dd if=/dev/zero of=/$TESTPOOL/$TESTFS/sync \
    oflag=sync bs=1 count=1
log_must sync_pool $TESTPOOL

#
# 1. Stop txgs from syncing on their own.  The sync thread picks up the new
#    timeout after the next txg.
#
log_must set_tunable32 TXG_TIMEOUT 3600
log_must sync_pool $TESTPOOL

#
# 2. The writes.
#
for ((i = 0; i < NREC; i++)); do
	dd if=$TESTDIR/w1 of=$file bs=64k count=1 seek=$((2 * i)) \
	    conv=notrunc 2>/dev/null || log_fail "W1[$i] failed"
done
for ((i = 0; i < NREC; i++)); do
	dd if=$TESTDIR/w2 of=$file bs=32k count=1 seek=$((4 * i + 2)) \
	    conv=notrunc 2>/dev/null || log_fail "W2[$i] failed"
done

#
# 3. Delay, then fail, writes to the data vdev, and fail its probes, so that
#    the txg sync suspends the pool.
#
log_must zinject -d ${vdevs[0]} -D 500:64 -T write $TESTPOOL
log_must zinject -d ${vdevs[0]} -e io -T write $TESTPOOL
log_must zinject -d ${vdevs[0]} -e nxio -T probe $TESTPOOL

#
# 4. fsync, which blocks when the pool suspends.
#
typeset -i zilerr=$(kstat zil.zil_commit_error_count)
dd if=/dev/null of=$file conv=notrunc,fsync &
typeset -i pid=$!

typeset -i tries=60
until [[ $(kstat_pool $TESTPOOL state) == "SUSPENDED" ]]; do
	((tries-- > 0)) || log_fail "pool didn't suspend"
	sleep 1
done
typeset -i zilerr2=$(kstat zil.zil_commit_error_count)
log_note "zil_commit_error_count $zilerr -> $zilerr2"
((zilerr2 > zilerr)) || log_fail "the log write didn't fail"
kill -0 $pid || log_fail "fsync returned while the pool was suspended"

#
# 5. Resume the pool.
#
log_must zinject -c all
log_must zpool clear $TESTPOOL
wait $pid || log_fail "fsync failed"
log_must restore_tunable TXG_TIMEOUT

#
# 6. Check the file.
#
log_must zpool export $TESTPOOL
log_must zpool import -d $VDIR $TESTPOOL
for ((i = 0; i < NREC; i++)); do
	dd if=$file of=$TESTDIR/r1 bs=64k count=1 skip=$((2 * i)) 2>/dev/null
	cmp -s $TESTDIR/r1 $TESTDIR/w1 || log_fail "W1[$i] is wrong"
	dd if=$file of=$TESTDIR/r2 bs=32k count=1 skip=$((4 * i + 2)) \
	    2>/dev/null
	cmp -s $TESTDIR/r2 $TESTDIR/w2 || log_fail "W2[$i] is wrong"
done

log_pass "A failed indirect write fails the log records that share it."
