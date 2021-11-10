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
# Copyright (c) 2018, Nexenta Systems, Inc.  All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_split/zpool_split.cfg

#
# DESCRIPTION:
# 'zpool split' should fail if resilver in progress for a disk
#
# STRATEGY:
# The first scenario:
# 1. Create a mirror pool
# 2. Offline the first VDEV
# 3. Put some data
# 4. Online the first VDEV
# 5. Verify 'zpool split' must fail
#
# The second scenario:
# 1. Create a mirror pool
# 2. Offline the second VDEV
# 3. Put some data
# 4. Online the second VDEV
# 5. Verify 'zpool split' must fail
#

verify_runnable "both"

function cleanup
{
	log_must set_tunable32 SCAN_SUSPEND_PROGRESS 0
	destroy_pool $TESTPOOL
	destroy_pool $TESTPOOL2
	rm -f $DEVICE1 $DEVICE2
}

function setup_mirror
{
	truncate -s $DEVSIZE $DEVICE1
	truncate -s $DEVSIZE $DEVICE2
	log_must zpool create -f $TESTPOOL mirror $DEVICE1 $DEVICE2
}

function zpool_split #disk_to_be_offline/online
{
	typeset disk=$1

	setup_mirror

	# offline a disk, so it will not be fully sync before split
	log_must zpool offline $TESTPOOL $disk

	# Create 2G of additional data
	mntpnt=$(get_prop mountpoint $TESTPOOL)
	log_must file_write -b 2097152 -c 1024 -o create -d 0 -f $mntpnt/biggerfile
	log_must sync

	# temporarily prevent resilvering progress, so it will not finish too early
	log_must set_tunable32 SCAN_SUSPEND_PROGRESS 1

	log_must zpool online $TESTPOOL $disk

	typeset i=0
	while ! is_pool_resilvering $TESTPOOL; do
		if (( i > 10 )); then
			log_fail "resilvering is not started"
		fi
		((i += 1))
		sleep 1
	done

	log_mustnot zpool split $TESTPOOL $TESTPOOL2

	log_must set_tunable32 SCAN_SUSPEND_PROGRESS 0
}

log_assert "Verify 'zpool split' will fail if resilver in progress for a disk"
log_onexit cleanup

DEVSIZE='3g'
DEVICE1="$TEST_BASE_DIR/device-1"
DEVICE2="$TEST_BASE_DIR/device-2"

log_note "Verify ZFS prevents main pool corruption during 'split'"
zpool_split $DEVICE1

cleanup

log_note "Verify ZFS prevents new pool corruption during 'split'"
zpool_split $DEVICE2

log_pass "'zpool split' failed as expected"
