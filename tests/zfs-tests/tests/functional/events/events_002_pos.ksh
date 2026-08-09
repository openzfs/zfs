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
# Verify ZED handles missed events from a pool when starting.
#
# STRATEGY:
# 1. Clear the events and create a pool to generate some events.
# 2. Start the ZED and verify it handles missed events.
# 3. Stop the ZED
# 4. Generate additional events.
# 5. Start the ZED and verify it only handles the new missed events.

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/events/events_common.kshlib

verify_runnable "both"

function cleanup
{
	poolexists $MPOOL && log_must destroy_pool $MPOOL
	log_must rm -f $VDEV1 $VDEV2 $TMP_EVENTS_ZED
	log_must zed_stop
}

log_assert "Verify ZED handles missed events when starting"
log_onexit cleanup

log_must truncate -s $MINVDEVSIZE $VDEV1 $VDEV2

# 1. Create a pool and generate some events.
log_must truncate -s 0 $ZED_DEBUG_LOG
log_must zpool events -c
log_must zpool create -O compression=off $MPOOL mirror $VDEV1 $VDEV2

# 2. Start the ZED and verify it handles missed events.
log_must zed_start
log_must file_wait_event $ZED_DEBUG_LOG 'sysevent\.fs\.zfs\.config_sync' 150
log_must cp $ZED_DEBUG_LOG $TMP_EVENTS_ZED

log_mustnot awk -v event="sysevent.fs.zfs.pool_create" -v crit="\\nZEVENT_POOL=$MPOOL" \
    'BEGIN{FS="\n"; RS=""} $0 ~ event && $0 ~ crit { exit 1 }' \
    $TMP_EVENTS_ZED

# 3. Stop the ZED
zed_stop
log_must truncate -s 0 $ZED_DEBUG_LOG

# 4. Generate additional events.
log_must zpool offline $MPOOL $VDEV1
log_must zpool online $MPOOL $VDEV1
log_must zpool wait -t resilver $MPOOL

log_must zpool scrub $MPOOL

# Wait for the scrub to wrap, or is_healthy will be wrong.
while ! is_pool_scrubbed $MPOOL; do
	sleep 1
done

# 5. Start the ZED and verify it only handled the new missed events.
log_must zed_start
log_must file_wait_event $ZED_DEBUG_LOG 'sysevent\.fs\.zfs\.resilver_finish' 150
log_must cp $ZED_DEBUG_LOG $TMP_EVENTS_ZED

log_mustnot file_wait_event $ZED_DEBUG_LOG 'sysevent\.fs\.zfs\.pool_create' 30
log_must grep -q "sysevent.fs.zfs.vdev_online" $TMP_EVENTS_ZED
log_must grep -q "sysevent.fs.zfs.resilver_start" $TMP_EVENTS_ZED

log_pass "Verify ZED handles missed events on when starting"
