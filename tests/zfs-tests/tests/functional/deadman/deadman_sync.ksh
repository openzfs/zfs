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
#	Verify spa deadman detects a hung txg
#
# STRATEGY:
#	1. Reduce the zfs_deadman_synctime_ms to 5s.
#	2. Reduce the zfs_deadman_checktime_ms to 1s.
#	3. Inject a 10s zio delay to force long IOs.
#	4. Write enough data to force a long txg sync time due to the delay.
#	5. Verify a "deadman" event is posted.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/deadman/deadman.cfg

verify_runnable "both"

function cleanup
{
	log_must zinject -c all
	default_cleanup_noexit

	log_must set_tunable64 DEADMAN_SYNCTIME_MS $SYNCTIME_DEFAULT
	log_must set_tunable64 DEADMAN_CHECKTIME_MS $CHECKTIME_DEFAULT
	log_must set_tunable64 DEADMAN_FAILMODE $FAILMODE_DEFAULT
}

log_assert "Verify spa deadman detects a hung txg"
log_onexit cleanup

log_must set_tunable64 DEADMAN_SYNCTIME_MS 5000
log_must set_tunable64 DEADMAN_CHECKTIME_MS 1000
log_must set_tunable64 DEADMAN_FAILMODE "wait"

# Create a new pool in order to use the updated deadman settings.
default_setup_noexit $DISK1
log_must zpool events -c

# Force each IO to take 10s but allow them to run concurrently.
log_must zinject -d $DISK1 -D10000:10 $TESTPOOL

mntpnt=$(get_prop mountpoint $TESTPOOL/$TESTFS)
log_must file_write -b 1048576 -c 8 -o create -d 0 -f $mntpnt/file
sleep 10

log_must zinject -c all
sync_all_pools

# Log txg sync times for reference and the zpool event summary.
log_must kstat_pool $TESTPOOL txgs
log_must zpool events

# Verify at least 3 deadman events were logged.  The first after 5 seconds,
# and another each second thereafter until the delay  is clearer.
events=$(zpool events | grep -c ereport.fs.zfs.deadman)
if [ "$events" -lt 3 ]; then
	log_fail "Expect >=3 deadman events, $events found"
fi

log_pass "Verify spa deadman detected a hung txg and $events deadman events"
