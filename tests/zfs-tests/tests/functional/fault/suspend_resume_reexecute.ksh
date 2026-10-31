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

#
# DESCRIPTION:
#	Resuming a suspended pool must not lose suspended I/O when, during
#	the resume, some of it fails again in a way that suspends the pool
#	while other I/O fails in a way that must be retried at once.
#
# STRATEGY:
#	1. Create a failmode=wait pool on two file vdevs, A and B, and a
#	   file with a block whose only copy is on A.
#	2. Make A fail to open, and reopen the pool, so A is unavailable.
#	   Make writes to B fail, and B's probes fail, then dirty some data:
#	   the txg sync can't allocate anywhere and the pool suspends.
#	3. Read the block on A: the read fails with ENXIO and is suspended
#	   too.
#	4. Let B's probes succeed again, but keep its writes failing, and
#	   run "zpool clear".  On resume, the txg sync's writes fail again
#	   and must be retried at once, while the read fails again and
#	   suspends the pool.  Let the writes be retried for 10 seconds.
#	5. Let B's writes succeed, then clear all injection and run
#	   "zpool clear" again.  Both "zpool clear" commands and the read
#	   must complete, the txg sync must complete, and the dirty data
#	   must be intact.
#
#	The pool's suspend and resume messages in the kernel log show when
#	the read has been suspended and when "zpool clear" has resumed the
#	pool.  B's write errors are cleared a few seconds before the rest, so
#	that no retried write is still in flight when injection is turned off:
#	that trips an assertion in zio_dva_throttle_done() on debug builds.
#
#	The test has only been run on Linux, so it is listed only in
#	linux.run.  It needs no special devices, only the kernel log.
#

. $STF_SUITE/include/libtest.shlib

verify_runnable "global"

VDEV_A=$TEST_BASE_DIR/reexec-a
VDEV_B=$TEST_BASE_DIR/reexec-b
SRC=$TEST_BASE_DIR/reexec-src
ZDBOUT=$TEST_BASE_DIR/reexec-zdb
FS=$TESTPOOL/$TESTFS
typeset wr="" read_pid="" clear_pid="" clear2_pid="" sync_pid=""

function cleanup
{
	typeset pid stuck=""

	# As in the test, let retried writes finish before injection is off.
	if [[ -n $wr ]]; then
		zinject -c $wr >/dev/null 2>&1
		sleep 5
	fi
	zinject -c all >/dev/null 2>&1
	if pool_is_suspended; then
		zpool clear $TESTPOOL >/dev/null 2>&1 &
		pid=$!
		if ! wait_for 60 is_done $pid; then
			log_note "zpool clear in cleanup hung"
			stuck=1
		fi
	fi
	#
	# Wait for the commands the test started, which keep the pool busy.
	# The zpool commands can't be killed while they wait for the pool, so
	# don't wait forever.  If one of them is stuck, or the pool is still
	# suspended, the pool can't be destroyed, so leave it, and its vdev
	# files, in place.
	#
	for pid in $read_pid $clear_pid $clear2_pid $sync_pid; do
		kill $pid 2>/dev/null
		if ! wait_for 60 is_done $pid; then
			log_note "process $pid did not exit"
			stuck=1
		fi
	done
	if [[ -n $stuck ]] || pool_is_suspended; then
		log_note "not destroying $TESTPOOL"
	else
		poolexists $TESTPOOL && destroy_pool $TESTPOOL
		rm -f $VDEV_A $VDEV_B
	fi
	rm -f $SRC $ZDBOUT
}

function pool_is_suspended
{
	[[ $(kstat_pool $TESTPOOL state) == "SUSPENDED" ]]
}

function pool_is_not_suspended
{
	! pool_is_suspended
}

function is_running # pid
{
	kill -0 $1 2>/dev/null
}

function is_done # pid
{
	! kill -0 $1 2>/dev/null
}

#
# log_fail prints its message only after the cleanup, which can take minutes
# if the pool is stuck, so log the reason first.
#
function fail
{
	log_note "$*"
	log_fail "$*"
}

function wait_for # seconds command ...
{
	typeset -i tries=$1
	shift
	while ((tries-- > 0)); do
		"$@" && return 0
		sleep 1
	done
	return 1
}

#
# The number of times the pool has been suspended or resumed, from the
# kernel log.
#
function suspensions
{
	dmesg | grep -c "Pool '$TESTPOOL' has encountered an uncorrectable"
}

function resumptions
{
	dmesg | grep -c "Pool '$TESTPOOL' was suspended and is being resumed"
}

function suspended_since # count
{
	(($(suspensions) > $1))
}

function resumed_since # count
{
	(($(resumptions) > $1))
}

log_assert "Resuming a pool does not lose I/O that must be reexecuted"
log_onexit cleanup

log_must truncate -s $MINVDEVSIZE $VDEV_A $VDEV_B
log_must zpool create -f -o failmode=wait $TESTPOOL $VDEV_A $VDEV_B
log_must zfs create -o recordsize=128k -o compression=off \
    -o primarycache=metadata $FS
log_must dd if=/dev/urandom of=/$FS/file bs=128k count=64
log_must dd if=/dev/urandom of=$SRC bs=128k count=8
log_must zpool sync $TESTPOOL

#
# Find a block of the file whose only copy is on A (vdev 0), and one on B.
#
typeset obj=$(ls -i /$FS/file | awk '{print $1}')
log_must eval "zdb -ddddd $FS $obj > $ZDBOUT"
typeset hexa=$(awk '$2 == "L0" && $3 ~ /^0:/ {print $1; exit}' $ZDBOUT)
typeset hexb=$(awk '$2 == "L0" && $3 ~ /^1:/ {print $1; exit}' $ZDBOUT)
[[ -n $hexa && -n $hexb ]] || log_fail "file is not spread over both vdevs"
typeset -i blka=$((16#$hexa / 131072)) blkb=$((16#$hexb / 131072))
log_note "block $blka of the file is on $VDEV_A, block $blkb on $VDEV_B"

#
# Drop the file's blocks from memory, then read the block on B, so that
# the file's metadata is cached and reading the block on A needs only that
# block.
#
log_must zpool export $TESTPOOL
log_must zpool import -d $TEST_BASE_DIR $TESTPOOL
log_must dd if=/$FS/file of=/dev/null bs=128k skip=$blkb count=1

# Make A unavailable, and suspend the txg sync.
log_must zinject -d $VDEV_A -e nxio $TESTPOOL
log_must zpool reopen $TESTPOOL
wr=$(zinject -d $VDEV_B -e io -T write $TESTPOOL | \
    awk '/Added handler/ {print $3}')
[[ -n $wr ]] || log_fail "could not inject write errors on $VDEV_B"
typeset pad2=$(zinject -d $VDEV_B -L pad2 $TESTPOOL | \
    awk '/Added handler/ {print $3}')
[[ -n $pad2 ]] || log_fail "could not inject probe errors on $VDEV_B"
log_must cp $SRC /$FS/dirty
wait_for 60 pool_is_suspended || log_fail "pool did not suspend"

# Suspend a read of the block on A.
typeset -i nsusp=$(suspensions)
dd if=/$FS/file of=/dev/null bs=128k skip=$blka count=1 &
read_pid=$!
wait_for 60 suspended_since $nsusp || \
    log_fail "read of $VDEV_A was not suspended"
is_running $read_pid || log_fail "read of $VDEV_A did not block"

#
# Resume while B's writes still fail, so that the txg sync must be
# reexecuted at once, and the read suspends the pool again.
#
log_must zinject -c $pad2
typeset -i nres=$(resumptions)
nsusp=$(suspensions)
zpool clear $TESTPOOL &
clear_pid=$!
wait_for 60 resumed_since $nres || fail "first zpool clear did not resume"
wait_for 60 suspended_since $nsusp || fail "read was not suspended again"
pool_is_suspended || fail "pool did not suspend again"

# Let the txg sync's writes be retried for a while.
sleep 10

log_must zinject -c $wr
wr=""
sleep 5
log_must zinject -c all
zpool clear $TESTPOOL &
clear2_pid=$!
wait_for 60 is_done $clear2_pid || fail "second zpool clear hung"
wait $clear2_pid || fail "second zpool clear failed"
clear2_pid=""
wait_for 60 is_done $read_pid || fail "read hung"
wait $read_pid || fail "read failed"
read_pid=""
wait_for 60 is_done $clear_pid || fail "first zpool clear hung"
wait $clear_pid || fail "first zpool clear failed"
clear_pid=""
pool_is_not_suspended || fail "pool still suspended"

zpool sync $TESTPOOL &
sync_pid=$!
wait_for 60 is_done $sync_pid || fail "txg sync hung"
wait $sync_pid || fail "txg sync failed"
sync_pid=""
log_must zpool export $TESTPOOL
log_must zpool import -d $TEST_BASE_DIR $TESTPOOL
log_must cmp $SRC /$FS/dirty

log_pass "Resuming a pool does not lose I/O that must be reexecuted"
