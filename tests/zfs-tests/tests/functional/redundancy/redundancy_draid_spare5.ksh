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
# Copyright (c) 2020 by Lawrence Livermore National Security, LLC.
# Copyright (c) 2026 by Wasabi Technologies, Inc.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/redundancy/redundancy.kshlib

#
# DESCRIPTION:
# Verify non-target disk clear during dRAID rebuild, it should not
# detach its draid spare after the rebuild is complete. Else, if
# more than N disks are cleared in draidN, we may lose data.
#
# STRATEGY:
# 1. Create a draid1 pool and fill it with data.
# 2. Engage 3 distributed spares and verify the pool.
# 3. Clear the 1st vdev while doing the rebuild of the 2nd vdev.
# 4. Clear the 2nd vdev while doing the rebuild of the 3rd vdev.
# 5. Refill the filesystem with new data.
# 6. Clear the pool to online previous faulted devices and resilver
# 7. Verify the pool and its contents
#

log_assert "Verify disk clear during rebuild on multiple dRAID spares"

log_must set_tunable32 REBUILD_SCRUB_ENABLED 0

function cleanup_tunable
{
	log_must set_tunable32 SCAN_SUSPEND_PROGRESS 0
	log_must set_tunable32 REBUILD_SCRUB_ENABLED 1
	cleanup
}

log_onexit cleanup_tunable

parity=1
spares=3
data=$(random_int_between 2 4)
children=10
draid="draid${parity}:${data}d:${children}c:${spares}s"

setup_test_env $TESTPOOL $draid $children

# Replace vdev7 -> draid1-0-0
log_must zpool offline -f $TESTPOOL $BASEDIR/vdev7
log_must zpool replace -sw $TESTPOOL $BASEDIR/vdev7 draid1-0-0

# Replace vdev8 -> draid1-0-1, clearing vdev7 while the rebuild is held
log_must zpool offline -f $TESTPOOL $BASEDIR/vdev8
log_must set_tunable32 SCAN_SUSPEND_PROGRESS 1
log_must zpool replace -s $TESTPOOL $BASEDIR/vdev8 draid1-0-1
log_must zpool clear $TESTPOOL $BASEDIR/vdev7
log_must set_tunable32 SCAN_SUSPEND_PROGRESS 0
log_must zpool wait -t resilver $TESTPOOL
zpool status -v $TESTPOOL
zpool history -i $TESTPOOL | tail -n 30
log_must check_hotspare_state $TESTPOOL draid1-0-0 "INUSE"

# Replace vdev9 -> draid1-0-2, clearing vdev8 while the rebuild is held
log_must zpool offline -f $TESTPOOL $BASEDIR/vdev9
log_must set_tunable32 SCAN_SUSPEND_PROGRESS 1
log_must zpool replace -s $TESTPOOL $BASEDIR/vdev9 draid1-0-2
log_must zpool clear $TESTPOOL $BASEDIR/vdev8
log_must set_tunable32 SCAN_SUSPEND_PROGRESS 0
log_must zpool wait -t resilver $TESTPOOL
zpool status -v $TESTPOOL
zpool history -i $TESTPOOL | tail -n 30
log_must check_hotspare_state $TESTPOOL draid1-0-0 "INUSE"
log_must check_hotspare_state $TESTPOOL draid1-0-1 "INUSE"

# Verify, refill and verify the pool contents.
log_must verify_draid_pool $TESTPOOL "sequential"
refill_test_env $TESTPOOL
log_must verify_draid_pool $TESTPOOL "sequential"

# Bring everything back online and check for errors.
log_must zpool clear $TESTPOOL
log_must zpool wait -t resilver $TESTPOOL

log_must wait_hotspare_state $TESTPOOL draid1-0-0 "AVAIL"
log_must wait_hotspare_state $TESTPOOL draid1-0-1 "AVAIL"
log_must wait_hotspare_state $TESTPOOL draid1-0-2 "AVAIL"

log_must verify_draid_pool $TESTPOOL "healing"

log_must is_data_valid $TESTPOOL

log_pass "Verify disk clear during rebuild on multiple dRAID spares"
