#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
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
# Copyright (c) 2024, Klara Inc.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/include/blkdev.shlib

#
# DESCRIPTION: Verify that 4 disks removed from a raidz3 will suspend the pool
#
# STRATEGY:
# 1. Disable ZED -- this test is focused on vdev_probe errors
# 2. Create a raidz3 pool where 4 disks can be removed (i.e., using scsi_debug)
# 3. Add some data to it for a resilver workload
# 4. Replace one of the child vdevs to start a replacing vdev
# 5. During the resilver, remove 4 disks including one from the replacing vdev
# 6. Verify that the pool is suspended (it used to remain online)
#

DEV_SIZE_MB=1024

FILE_VDEV_CNT=8
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

log_assert "VDEV probe errors for more disks than parity should suspend a pool"

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

# Create 8 file vdevs
typeset -a filedevs
for i in {0..$((FILE_VDEV_CNT - 1))}; do
	device=$TEST_BASE_DIR/dev-$i
	log_must truncate -s $FILE_VDEV_SIZ $device
	# Use all but the last one for pool create
	if [[ $i -lt "7" ]]; then
		filedevs[${#filedevs[*]}+1]=$device
	fi
done

# Create a raidz-3 pool that we can pull 4 disks from
log_must zpool create -f $TESTPOOL raidz3 ${filedevs[@]} $blkdevs
sync_pool $TESTPOOL

# Add some data to the pool
log_must zfs create $TESTPOOL/fs
MNTPOINT="$(get_prop mountpoint $TESTPOOL/fs)"
SECONDS=0
log_must fill_fs $MNTPOINT 1 200 4096 10 Z
log_note "fill_fs took $SECONDS seconds"
sync_pool $TESTPOOL

# Start a replacing vdev, but suspend the resilver
log_must set_tunable32 SCAN_SUSPEND_PROGRESS 1
log_must zpool replace -f $TESTPOOL /dev/${sd}4 $TEST_BASE_DIR/dev-7

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

log_pass "VDEV probe errors for more disks than parity should suspend a pool"
