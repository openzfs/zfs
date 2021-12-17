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
# Portions Copyright 2021 iXsystems, Inc.
#

# DESCRIPTION:
#	Verify spa deadman events are rate limited
#
# STRATEGY:
#	1. Reduce the zfs_slow_io_events_per_second to 1.
#	2. Reduce the zfs_deadman_ziotime_ms to 1ms.
#	3. Write data to a pool and read it back.
#	4. Verify deadman events have been produced at a reasonable rate.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/deadman/deadman.cfg

verify_runnable "both"

function cleanup
{
	zinject -c all
	default_cleanup_noexit

	set_tunable64 SLOW_IO_EVENTS_PER_SECOND $OLD_SLOW_IO_EVENTS
	set_tunable64 DEADMAN_ZIOTIME_MS $ZIOTIME_DEFAULT
}

log_assert "Verify spa deadman events are rate limited"
log_onexit cleanup

OLD_SLOW_IO_EVENTS=$(get_tunable SLOW_IO_EVENTS_PER_SECOND)
log_must set_tunable64 SLOW_IO_EVENTS_PER_SECOND 1
log_must set_tunable64 DEADMAN_ZIOTIME_MS 1

# Create a new pool in order to use the updated deadman settings.
default_setup_noexit $DISK1
log_must zpool events -c

mntpnt=$(get_prop mountpoint $TESTPOOL/$TESTFS)
log_must file_write -b 1048576 -c 8 -o create -d 0 -f $mntpnt/file
log_must zpool export $TESTPOOL
log_must zpool import $TESTPOOL
log_must zinject -d $DISK1 -D 5:1 $TESTPOOL
log_must dd if=$mntpnt/file of=/dev/null oflag=sync

events=$(zpool events $TESTPOOL | grep -c ereport.fs.zfs.deadman)
log_note "events=$events"
if [ "$events" -lt 1 ]; then
	log_fail "Expect >= 1 deadman events, $events found"
fi
if [ "$events" -gt 10 ]; then
	log_fail "Expect <= 10 deadman events, $events found"
fi

log_pass "Verify spa deadman events are rate limited"
