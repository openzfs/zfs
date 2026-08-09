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
# Copyright (c) 2017 by Lawrence Livermore National Security, LLC.
# Use is subject to license terms.
#

# DESCRIPTION:
#	Verify zio deadman detects a hung zio
#
# STRATEGY:
#	1. Reduce the zfs_deadman_ziotime_ms to 5s.
#	2. Reduce the zfs_deadman_checktime_ms to 1s.
#	3. Inject a 10s zio delay to force long IOs.
#	4. Read an uncached file in the background.
#	5. Verify a "deadman" event is posted.
#	6. Inject a 100ms zio delay which is under the 5s allowed.
#	7. Read an uncached file in the background.
#	8. Verify a "deadman" event is not posted.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/deadman/deadman.cfg

verify_runnable "both"

function cleanup
{
	log_must zinject -c all
	default_cleanup_noexit

	log_must set_tunable64 DEADMAN_ZIOTIME_MS $ZIOTIME_DEFAULT
	log_must set_tunable64 DEADMAN_CHECKTIME_MS $CHECKTIME_DEFAULT
	log_must set_tunable64 DEADMAN_FAILMODE $FAILMODE_DEFAULT
}

log_assert "Verify zio deadman detects a hung zio"
log_onexit cleanup

# 1. Reduce the zfs_deadman_ziotime_ms to 5s.
log_must set_tunable64 DEADMAN_ZIOTIME_MS 5000
# 2. Reduce the zfs_deadman_checktime_ms to 1s.
log_must set_tunable64 DEADMAN_CHECKTIME_MS 1000
log_must set_tunable64 DEADMAN_FAILMODE "wait"

# Create a new pool in order to use the updated deadman settings.
default_setup_noexit $DISK1

# Write a file and export/import the pool to clear the ARC.
mntpnt=$(get_prop mountpoint $TESTPOOL/$TESTFS)
log_must file_write -b 1048576 -c 8 -o create -d 0 -f $mntpnt/file1
log_must file_write -b 1048576 -c 8 -o create -d 0 -f $mntpnt/file2
log_must zpool export $TESTPOOL
log_must zpool import $TESTPOOL
log_must stat -t /$mntpnt/file1
log_must stat -t /$mntpnt/file2

# 3. Inject a 10s zio delay to force long IOs and serialize them..
log_must zpool events -c
log_must zinject -d $DISK1 -D10000:1 $TESTPOOL

# 4. Read an uncached file in the background, it's expected to hang.
log_must eval "dd if=/$mntpnt/file1 of=/dev/null bs=1048576 &"
sleep 10
log_must zinject -c all
sync_all_pools
wait

# 5. Verify a "deadman" event is posted.  The first appears after 5
# seconds, and another each second thereafter until the delay is cleared.
events=$(zpool events | grep -c ereport.fs.zfs.deadman)
if [ "$events" -lt 1 ]; then
	log_fail "Expect >=1 deadman events, $events found"
fi

# 6. Inject a 100ms zio delay which is under the 5s allowed, allow them
# to run concurrently so they don't get starved in the queue.
log_must zpool events -c
log_must zinject -d $DISK1 -D100:10 $TESTPOOL

# 7. Read an uncached file in the background.
log_must eval "dd if=/$mntpnt/file2 of=/dev/null bs=1048576 &"
sleep 10
log_must zinject -c all
wait

# 8. Verify a "deadman" event is not posted.
events=$(zpool events | grep -c ereport.fs.zfs.deadman)
if [ "$events" -ne 0 ]; then
	log_fail "Expect 0 deadman events, $events found"
fi

log_pass "Verify zio deadman detected a hung zio and $events deadman events"
