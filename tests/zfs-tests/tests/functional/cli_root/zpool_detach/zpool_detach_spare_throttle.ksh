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
# Copyright (c) 2026 by the OpenZFS project.  All rights reserved.
#
. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#	Detaching the original device of an in-use hot spare completes
#	while the pool is at its dirty data limit.  The detach writes a
#	history record for the spare it removes; that must not wait for
#	a txg sync while it holds the config lock, which the sync needs.
#
# STRATEGY:
#	1. Create a pool with one device and a hot spare, and replace the
#	   device with the spare
#	2. Lower zfs_dirty_data_max, turn off the write throttle's delays,
#	   slow down writes with zinject and keep the pool at its dirty
#	   data limit with a writer
#	3. Detach the original device; it must finish within the timeout
#	4. Check that the spare is now a normal device of the pool and
#	   that the history has the "detach" and the spare's "vdev remove"
#	   records
#

verify_runnable "global"

log_assert "Detaching a spared device at the dirty data limit completes"

typeset -i TIMEOUT=60
typeset -i dpid=0
typeset -i wpid=0
typeset DEVDIR=$TEST_BASE_DIR/zpool_detach_spare_throttle
typeset ORIG=$DEVDIR/orig
typeset SPARE=$DEVDIR/spare

function cleanup
{
	if ((wpid > 0)); then
		kill $wpid 2>/dev/null
		wait $wpid 2>/dev/null
	fi
	if ((dpid > 0)) && kill -0 $dpid 2>/dev/null; then
		#
		# The pool is deadlocked and nothing here can undo that:
		# zinject and zpool destroy would hang too.
		#
		log_note "zpool detach is still running; reboot to recover"
		restore_tunable DIRTY_DATA_MAX
		restore_tunable DELAY_SCALE
		return
	fi
	zinject -c all
	restore_tunable DIRTY_DATA_MAX
	restore_tunable DELAY_SCALE
	poolexists $TESTPOOL1 && destroy_pool $TESTPOOL1
	rm -rf $DEVDIR
}

log_must save_tunable DIRTY_DATA_MAX
log_must save_tunable DELAY_SCALE
log_onexit cleanup

log_must mkdir -p $DEVDIR
#
# The devices must be large enough that the writer is held back by the
# dirty data limit, not by space reservations.
#
log_must truncate -s $((MINVDEVSIZE * 4)) $ORIG $SPARE
log_must zpool create -f -O compression=off $TESTPOOL1 $ORIG spare $SPARE
log_must zpool replace $TESTPOOL1 $ORIG $SPARE
log_must zpool wait -t resilver $TESTPOOL1
log_must check_hotspare_state $TESTPOOL1 $SPARE "INUSE"

#
# Without the write throttle's delays, the writer fills the pool to its
# dirty data limit and then waits for a txg sync to make room, so the
# pool stays at the limit.
#
log_must set_tunable64 DIRTY_DATA_MAX $((16 * 1024 * 1024))
log_must set_tunable64 DELAY_SCALE 0
for dev in $ORIG $SPARE; do
	log_must zinject -d $dev -D 20:1 -T write $TESTPOOL1
done

typeset mntpnt=$(get_prop mountpoint $TESTPOOL1)
(
	trap 'kill $ddpid 2>/dev/null; exit' TERM
	while true; do
		dd if=/dev/zero of=$mntpnt/file bs=1M count=64 \
		    conv=notrunc 2>/dev/null &
		ddpid=$!
		wait $ddpid
	done
) &
wpid=$!
sleep 5

zpool detach $TESTPOOL1 $ORIG &
dpid=$!

typeset -i waited=0
while kill -0 $dpid 2>/dev/null; do
	((waited++ >= TIMEOUT)) && \
	    log_fail "zpool detach still running after ${TIMEOUT}s"
	sleep 1
done
wait $dpid
typeset -i rc=$?
dpid=0
log_note "zpool detach returned $rc after about ${waited}s"
log_must [ $rc -eq 0 ]

kill $wpid
wait $wpid 2>/dev/null
wpid=0
log_must zinject -c all

if zpool status -P $TESTPOOL1 | grep -q $ORIG; then
	log_fail "$ORIG is still in $TESTPOOL1"
fi
log_must [ -z "$(get_device_state $TESTPOOL1 $SPARE spares)" ]
log_must eval "zpool history -i $TESTPOOL1 | grep 'detach' | grep -q $ORIG"
log_must eval "zpool history -i $TESTPOOL1 | grep 'vdev remove' | \
    grep -q $SPARE"

log_pass "Detaching a spared device at the dirty data limit completes"
