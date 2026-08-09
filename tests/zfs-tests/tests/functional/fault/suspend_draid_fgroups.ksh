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
# Copyright (c) 2024, Klara Inc.
# Copyright (c) 2026, Seagate Technology, LLC.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/include/blkdev.shlib

#
# DESCRIPTION: Verify that 4 disks removed from a draid3 with failure
# groups, when they are removed from a group, will suspend the pool.
#
# STRATEGY:
# 1. Disable ZED -- this test is focused on vdev_probe errors.
# 2. Create a draid3 pool with random number of failure groups, from 2 to 6,
#    where 4 disks can be removed (i.e., using scsi_debug).
# 3. Add some data to it for a resilver workload.
# 4. Replace one of the child vdevs to start a replacing vdev.
# 5. During the resilver, remove 4 disks, including one from the replacing vdev,
#    from a failure group.
# 6. Verify that the pool is suspended.
#

DEV_SIZE_MB=1024

DRAID_FGRP_CNT=$(random_int_between 2 6)
FILE_VDEV_CNT=$((8 * $DRAID_FGRP_CNT))
DRAID="draid3:8c:${FILE_VDEV_CNT}w"
FILE_VDEV_SIZ=256M

function cleanup
{
	destroy_pool $TESTPOOL
	if [[ "$(cat /sys/block/$sd/device/state)" == "offline" ]]; then
		log_must eval "echo running > /sys/block/$sd/device/state"
	fi
	unload_scsi_debug
	rm -f $DATA_FILE
	for i in {0..$((FILE_VDEV_CNT - 1))}; do
		log_must rm -f "$TEST_BASE_DIR/dev-$i"
	done
	log_must set_tunable32 SCAN_SUSPEND_PROGRESS 0
	zed_start
}

log_onexit cleanup

log_assert "dRAID vdev with failure groups probe errors for more disks than" \
     "parity in a group should suspend a pool"

log_note "Stoping ZED process"
zed_stop
zpool events -c

# Make a debug device that we can "unplug" and lose 4 drives at once
unload_scsi_debug
load_scsi_debug $DEV_SIZE_MB 1 1 1 '512b'
sd=$(get_debug_device)

# Create 4 partitions that match the FILE_VDEV_SIZ
parted "/dev/${sd}" --script mklabel gpt
parted "/dev/${sd}" --script mkpart primary 0% 25%
parted "/dev/${sd}" --script mkpart primary 25% 50%
parted "/dev/${sd}" --script mkpart primary 50% 75%
parted "/dev/${sd}" --script mkpart primary 75% 100%
block_device_wait "/dev/${sd}"
blkdevs="/dev/${sd}1 /dev/${sd}2 /dev/${sd}3 /dev/${sd}4"

# Create file vdevs
typeset -a filedevs
for i in {0..$((FILE_VDEV_CNT - 1))}; do
	device=$TEST_BASE_DIR/dev-$i
	log_must truncate -s $FILE_VDEV_SIZ $device
	# Use all but the last one for pool create
	if [[ $i -lt $((FILE_VDEV_CNT - 4)) ]]; then
		filedevs[${#filedevs[*]}+1]=$device
	fi
done

# Create a draid3 pool that we can pull 4 disks from
log_must zpool create -f $TESTPOOL $DRAID ${filedevs[@]} $blkdevs
sync_pool $TESTPOOL

# Add some data to the pool
log_must zfs create $TESTPOOL/fs
MNTPOINT="$(get_prop mountpoint $TESTPOOL/fs)"
SECONDS=0
log_must fill_fs $MNTPOINT 1 200 4096 10 R
log_note "fill_fs took $SECONDS seconds"
sync_pool $TESTPOOL

# Start a replacing vdev, but suspend the resilver
log_must set_tunable32 SCAN_SUSPEND_PROGRESS 1
log_must zpool replace -f $TESTPOOL /dev/${sd}4 $TEST_BASE_DIR/dev-$((FILE_VDEV_CNT - 1))

# Remove 4 disks all at once
log_must eval "echo offline > /sys/block/${sd}/device/state"

log_must set_tunable32 SCAN_SUSPEND_PROGRESS 0

# Add some writes to drive the vdev probe errors
log_must dd if=/dev/urandom of=$MNTPOINT/writes bs=1M count=1

# Wait until sync starts, and the pool suspends
log_note "waiting for pool to suspend"
typeset -i tries=30
until [[ $(kstat_pool $TESTPOOL state) == "SUSPENDED" ]] ; do
	if ((tries-- == 0)); then
		zpool status -s
		log_fail "UNEXPECTED -- pool did not suspend"
	fi
	sleep 1
done
log_note $(kstat_pool $TESTPOOL state)

# Put the missing disks back into service
log_must eval "echo running > /sys/block/$sd/device/state"

# Clear the vdev error states, which will reopen the vdevs and resume the pool
log_must zpool clear $TESTPOOL

# Wait until the pool resumes
log_note "waiting for pool to resume"
tries=30
until [[ $(kstat_pool $TESTPOOL state) != "SUSPENDED" ]] ; do
	if ((tries-- == 0)); then
		log_fail "pool did not resume"
	fi
	sleep 1
done
log_must zpool wait -t resilver $TESTPOOL
sync_pool $TESTPOOL

# Make sure a pool scrub comes back clean
log_must zpool scrub -w $TESTPOOL
log_must zpool status -v $TESTPOOL
log_must check_pool_status $TESTPOOL "errors" "No known data errors"

log_pass "dRAID vdev with failure groups probe errors for more disks than" \
     "parity in a group should suspend a pool"
