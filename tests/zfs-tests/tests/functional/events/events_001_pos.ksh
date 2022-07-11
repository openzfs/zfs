#!/bin/ksh -p
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or https://opensource.org/licenses/CDDL-1.0.
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
# Verify zpool events command logs events.
#
# STRATEGY:
# 1. Execute zpool sub-commands on a pool.
# 2. Verify the expected events are logged in 'zpool events'.
# 3. Verify the expected events are logged by the ZED.

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/events/events_common.kshlib

verify_runnable "both"

function cleanup
{
	if poolexists $MPOOL; then
		destroy_pool $MPOOL
	fi

	for file in $VDEV1 $VDEV2 $VDEV3 $VDEV4; do
		[[ -f $file ]] && rm -f $file
	done

	log_must zed_stop
}

log_assert "Verify zpool sub-commands generate expected events"
log_onexit cleanup

log_must truncate -s $MINVDEVSIZE $VDEV1 $VDEV2 $VDEV3 $VDEV4

log_must zpool events -c
log_must zed_start

# Create a mirrored pool with two devices.
run_and_verify -p "$MPOOL" \
    -e "sysevent.fs.zfs.pool_create" \
    -e "sysevent.fs.zfs.history_event" \
    -e "sysevent.fs.zfs.config_sync" \
    "zpool create $MPOOL mirror $VDEV1 $VDEV2"

# Set a pool property.
run_and_verify -p "$MPOOL" \
    -e "sysevent.fs.zfs.history_event" \
    "zpool set comment=string $MPOOL"

# Add a cache device then remove it.
run_and_verify -p "$MPOOL" \
    -e "sysevent.fs.zfs.config_sync" \
    -e "sysevent.fs.zfs.vdev_add" \
    "zpool add -f $MPOOL spare $VDEV3"
run_and_verify -p "$MPOOL" \
    -e "sysevent.fs.zfs.vdev_remove_aux" \
    "zpool remove $MPOOL $VDEV3"

# Add a log device then remove it.
run_and_verify -p "$MPOOL" \
    -e "sysevent.fs.zfs.config_sync" \
    -e "sysevent.fs.zfs.vdev_add" \
    "zpool add -f $MPOOL log $VDEV3"
run_and_verify -p "$MPOOL" \
    -e "sysevent.fs.zfs.vdev_remove_dev" \
    "zpool remove $MPOOL $VDEV3"

# Offline then online a device.
run_and_verify -p "$MPOOL"\
    -e "resource.fs.zfs.statechange" \
    -e "sysevent.fs.zfs.config_sync" \
    "zpool offline $MPOOL $VDEV1"
run_and_verify -p "$MPOOL" \
    -e "resource.fs.zfs.statechange" \
    -e "sysevent.fs.zfs.vdev_online" \
    -e "sysevent.fs.zfs.config_sync" \
    -e "sysevent.fs.zfs.resilver_start" \
    -e "sysevent.fs.zfs.history_event" \
    -e "sysevent.fs.zfs.resilver_finish" \
    "zpool online $MPOOL $VDEV1"

# Attach then detach a device from the mirror.
run_and_verify -p "$MPOOL" \
    -e "sysevent.fs.zfs.vdev_attach" \
    -e "sysevent.fs.zfs.resilver_start" \
    -e "sysevent.fs.zfs.config_sync" \
    -e "sysevent.fs.zfs.history_event" \
    -e "sysevent.fs.zfs.resilver_finish" \
    "zpool attach $MPOOL $VDEV1 $VDEV4"
run_and_verify -p "$MPOOL" \
    -e "sysevent.fs.zfs.vdev_remove" \
    -e "sysevent.fs.zfs.config_sync" \
    "zpool detach $MPOOL $VDEV4"

# Replace a device
run_and_verify -p "$MPOOL" \
    -e "sysevent.fs.zfs.vdev_attach" \
    -e "sysevent.fs.zfs.resilver_start" \
    -e "sysevent.fs.zfs.config_sync" \
    -e "sysevent.fs.zfs.history_event" \
    -e "sysevent.fs.zfs.resilver_finish" \
    -e "sysevent.fs.zfs.vdev_remove" \
    "zpool replace -f $MPOOL $VDEV1 $VDEV4"

# Scrub a pool.
run_and_verify -p "$MPOOL" \
    -e "sysevent.fs.zfs.scrub_start" \
    -e "sysevent.fs.zfs.history_event" \
    -e "sysevent.fs.zfs.scrub_finish" \
    "zpool scrub $MPOOL"

# Export then import a pool
run_and_verify -p "$MPOOL" \
    -e "sysevent.fs.zfs.pool_export" \
    -e "sysevent.fs.zfs.config_sync" \
    "zpool export $MPOOL"
run_and_verify -p "$MPOOL" \
    -e "sysevent.fs.zfs.config_sync" \
    -e "sysevent.fs.zfs.history_event" \
    -e "sysevent.fs.zfs.pool_import" \
    "zpool import -d $TEST_BASE_DIR $MPOOL"

# Destroy the pool
run_and_verify -p "$MPOOL" \
    -e "sysevent.fs.zfs.pool_destroy" \
    -e "sysevent.fs.zfs.config_sync" \
    "zpool destroy $MPOOL"

log_pass "Verify zpool sub-commands generate expected events"
