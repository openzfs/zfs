#!/bin/ksh -p
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
# See the License for the specific language governing permissions
# and limitations under the License.
#
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
# CDDL HEADER END
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

# Force each IO to take 10s by allow them to run concurrently.
log_must zinject -d $DISK1 -D10000:10 $TESTPOOL

mntpnt=$(get_prop mountpoint $TESTPOOL/$TESTFS)
log_must file_write -b 1048576 -c 8 -o create -d 0 -f $mntpnt/file
sleep 10

log_must zinject -c all
log_must zpool sync

# Log txg sync times for reference and the zpool event summary.
if is_freebsd; then
	log_must sysctl -n kstat.zfs.$TESTPOOL.txgs
else
	log_must cat /proc/spl/kstat/zfs/$TESTPOOL/txgs
fi
log_must zpool events

# Verify at least 4 deadman events were logged.  The first after 5 seconds,
# and another each second thereafter until the delay  is clearer.
events=$(zpool events | grep -c ereport.fs.zfs.deadman)
if [ "$events" -lt 4 ]; then
	log_fail "Expect >=5 deadman events, $events found"
fi

log_pass "Verify spa deadman detected a hung txg and $events deadman events"
