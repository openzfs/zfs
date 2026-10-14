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
# Copyright (c) 2026 by Will Rouesnel. All rights reserved.
#

#
# DESCRIPTION:
#	When a pool is suspended while no txg is syncing, txg_sync must wait
#	for it to resume, not busy-loop.  txg_sync_thread() skipped the sync
#	of a suspended pool with a bare "continue".  Once its timer had
#	expired, or with a txg_wait_synced() caller waiting, nothing on the
#	way back round blocked, so the thread spun on a CPU, holding
#	tx_sync_lock, until the pool resumed.  An MMP suspension is the usual
#	way to get there; this test suspends the pool from the async thread
#	instead, which "zpool clear" can always resume.
#
# STRATEGY:
#	1. Create a pool on one file vdev, write a file and re-import the
#	   pool, so the file is read from disk.
#	2. Fail the vdev's reads, and the label writes of the vdev probe.
#	   Read the file: the read error starts a probe, the probe fails, and
#	   the async thread faults the only vdev and suspends the pool.
#	3. Write a second file, which waits in txg_wait_synced() for the
#	   pool to resume.
#	4. Once the txg timer has expired, verify that the txg_sync threads
#	   use almost no CPU, and that the read and the write still wait.
#	5. Clear the injection and the suspension.  Verify that the read and
#	   the write complete with the right data, the pool is healthy, and
#	   it exports and imports again.
#

. $STF_SUITE/include/libtest.shlib

verify_runnable "global"

VDEV=$TEST_BASE_DIR/suspend_txg_vdev
SRC=$TEST_BASE_DIR/suspend_txg_src
OUT=$TEST_BASE_DIR/suspend_txg_out
READ_PID=""
WRITE_PID=""
TXG_TIMEOUT_SAVED=$(get_tunable TXG_TIMEOUT)

function cleanup
{
	zinject -c all >/dev/null 2>&1
	if poolexists $TESTPOOL && \
	    [[ $(kstat_pool $TESTPOOL state) == "SUSPENDED" ]]; then
		log_must zpool clear $TESTPOOL
	fi
	[[ -n "$READ_PID" ]] && wait $READ_PID
	[[ -n "$WRITE_PID" ]] && wait $WRITE_PID
	poolexists $TESTPOOL && destroy_pool $TESTPOOL
	log_must rm -f $VDEV $SRC $OUT
	log_must set_tunable64 TXG_TIMEOUT $TXG_TIMEOUT_SAVED
}

# CPU time, in clock ticks, used so far by all txg_sync threads.
function txg_sync_ticks
{
	typeset pid ticks=0 t

	for pid in $(pgrep -x txg_sync); do
		# utime and stime are fields 14 and 15 of /proc/PID/stat.
		t=$(awk '{print $14 + $15}' /proc/$pid/stat 2>/dev/null)
		(( ticks += ${t:-0} ))
	done
	echo $ticks
}

log_assert "txg_sync waits, without spinning, while the pool is suspended"
log_onexit cleanup

typeset -i txg_timeout=5
log_must set_tunable64 TXG_TIMEOUT $txg_timeout

log_must truncate -s $MINVDEVSIZE $VDEV
log_must dd if=/dev/urandom of=$SRC bs=128k count=8
log_must zpool create -f $TESTPOOL $VDEV
log_must cp $SRC /$TESTPOOL/file
log_must zpool export $TESTPOOL
log_must zpool import -d $TEST_BASE_DIR $TESTPOOL

log_must zinject -d $VDEV -e io -L pad2 -T write -f 100 $TESTPOOL
log_must zinject -d $VDEV -e io -T read -f 100 $TESTPOOL
cat /$TESTPOOL/file > $OUT &
READ_PID=$!

typeset -i i=0
while [[ $(kstat_pool $TESTPOOL state) != "SUSPENDED" ]]; do
	(( i++ > 30 )) && log_fail "$TESTPOOL was not suspended"
	sleep 1
done
log_note "$TESTPOOL suspended after about $i seconds"

dd if=$SRC of=/$TESTPOOL/file2 bs=128k &
WRITE_PID=$!

# Let the txg timer expire: the thread spun from then on, waiter or not.
log_must sleep $(( txg_timeout + 2 ))

typeset -i hz=$(getconf CLK_TCK)
typeset -i secs=5
typeset -i before=$(txg_sync_ticks)
log_must sleep $secs
typeset -i used=$(( $(txg_sync_ticks) - before ))
log_note "txg_sync used $used of $(( secs * hz )) ticks in $secs seconds"
if (( used > secs * hz / 5 )); then
	log_fail "txg_sync spun on the suspended pool ($used ticks)"
fi

kill -0 $READ_PID 2>/dev/null || \
    log_fail "the read returned while $TESTPOOL was suspended"
kill -0 $WRITE_PID 2>/dev/null || \
    log_fail "the write returned while $TESTPOOL was suspended"

log_must zinject -c all
log_must zpool clear $TESTPOOL

wait $READ_PID
typeset -i rc=$?
READ_PID=""
(( rc == 0 )) || log_fail "the read failed after the resume ($rc)"
wait $WRITE_PID
rc=$?
WRITE_PID=""
(( rc == 0 )) || log_fail "the write failed after the resume ($rc)"

log_must cmp $SRC $OUT
log_must zpool sync $TESTPOOL
log_must check_state $TESTPOOL "" "ONLINE"
log_must cmp $SRC /$TESTPOOL/file2
log_must zpool export $TESTPOOL
log_must zpool import -d $TEST_BASE_DIR $TESTPOOL
log_must cmp $SRC /$TESTPOOL/file
log_must cmp $SRC /$TESTPOOL/file2

log_pass "txg_sync waits, without spinning, while the pool is suspended"
