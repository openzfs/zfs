#!/bin/ksh -p
#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# http://www.illumos.org/license/CDDL.
#

#
# Copyright 2017, loli10K <ezomori.nozomu@gmail.com>. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_events/zpool_events.kshlib

#
# DESCRIPTION:
# 'zpool events poolname' should only display events from the chosen pool.
#
# STRATEGY:
# 1. Create an additional pool
# 2. Clear all ZFS events
# 3. Generate some ZFS events on both pools
# 4. Verify 'zpool events poolname' successfully display events
#

verify_runnable "both"

function cleanup
{
	destroy_pool $NEWPOOL
	rm -f $DISK
}

log_assert "'zpool events poolname' should only display events from poolname."
log_onexit cleanup

NEWPOOL="newpool"
DISK="$TEST_BASE_DIR/$NEWPOOL.dat"

# 1. Create an additional pool
log_must truncate -s $MINVDEVSIZE $DISK
log_must zpool create $NEWPOOL $DISK

# 2. Clear all ZFS events
log_must zpool events -c

# 3. Generate some ZFS events on both pools
for i in {1..$EVENTS_NUM}; do
	log_must zpool clear $TESTPOOL
	log_must zpool clear $NEWPOOL
done
# wait a bit to allow the kernel module to process new events
zpool_events_settle

# 4. Verify 'zpool events poolname' successfully display events
zpool events -v $TESTPOOL |
   awk -v POOL=$TESTPOOL '/pool = / && $3 != "\""POOL"\"" {exit 1}' ||
	log_fail "Unexpected events for pools other than $TESTPOOL"

zpool events -v $NEWPOOL |
   awk -v POOL=$NEWPOOL '/pool = / && $3 != "\""POOL"\"" {exit 1}' ||
	log_fail "Unexpected events for pools other than $NEWPOOL"

log_pass "'zpool events poolname' display events only from the chosen pool."
