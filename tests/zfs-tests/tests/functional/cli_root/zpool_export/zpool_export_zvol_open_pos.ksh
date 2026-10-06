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

#
# DESCRIPTION:
#	Verify that zpool export does not hang while other processes keep
#	opening the pool's zvols.
#
# STRATEGY:
#	1. Create a pool with many zvols, so removing them takes a while
#	2. Make a zvol open fail at once when the namespace lock is busy
#	3. Open and close a few of the zvols in loops in the background
#	4. Export and import the pool several times
#

verify_runnable "global"

DEVICE_DIR=$TEST_BASE_DIR/dev_export-zvol-test
STOP_FILE=$TEST_BASE_DIR/export-zvol-stop
NUM_VOLS=200
LAST_VOL=$ZVOL_DEVDIR/$TESTPOOL1/vol$NUM_VOLS

function cleanup
{
	touch $STOP_FILE
	wait
	poolexists $TESTPOOL1 && destroy_pool $TESTPOOL1
	log_must rm -rf $DEVICE_DIR $STOP_FILE
	log_pos restore_tunable VOL_OPEN_TIMEOUT_MS
}

log_assert "zpool export does not hang while the pool's zvols are opened"

log_onexit cleanup

log_must mkdir -p $DEVICE_DIR
log_must truncate -s $MINVDEVSIZE $DEVICE_DIR/disk
log_must zpool create -f -O mountpoint=none $TESTPOOL1 $DEVICE_DIR/disk
for i in {1..$NUM_VOLS}; do
	log_must zfs create -s -V 8M -o volmode=dev $TESTPOOL1/vol$i
done
block_device_wait $LAST_VOL

# Make a zvol open that finds the namespace lock busy fail at once.
# Otherwise it sleeps in the kernel until the next tick, and the openers
# rarely hit the short window after export drops the lock.
log_must save_tunable VOL_OPEN_TIMEOUT_MS
if tunable_exists VOL_OPEN_TIMEOUT_MS; then
	log_must set_tunable32 VOL_OPEN_TIMEOUT_MS 0
fi

# Open with true. With the colon builtin, a failed open ends the loop.
# Try again at once while the device exists. A few openers are enough.
# With one per zvol, the system gets so busy that an opener can stall
# with its zvol open, and export then fails because the pool is busy.
log_must rm -f $STOP_FILE
for i in {1..4}; do
	dev=$ZVOL_DEVDIR/$TESTPOOL1/vol$i
	while [[ ! -e $STOP_FILE ]]; do
		true <$dev || [[ -e $dev ]] || sleep 0.1
	done 2>/dev/null &
done

for i in {1..10}; do
	log_must_busy zpool export $TESTPOOL1
	log_must zpool import -d $DEVICE_DIR $TESTPOOL1
	block_device_wait $LAST_VOL
done

log_pass "zpool export does not hang while the pool's zvols are opened"
