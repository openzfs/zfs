#!/bin/ksh -p
#
# CDDL HEADER START
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
# CDDL HEADER END
#

#
# Copyright 2018, loli10K <ezomori.nozomu@gmail.com>. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/events/events_common.kshlib
. $STF_SUITE/tests/functional/fault/fault.cfg

#
# DESCRIPTION:
# Testing Fault Management Agent ZED Logic - Physically detached device is
# made removed and onlined when reattached
#
# STRATEGY:
# 1. Create a pool
# 2. Simulate physical removal of one device
# 3. Verify the device is removed when detached
# 4. Reattach the device
# 5. Verify the device is onlined
# 6. Repeat the same tests with a spare device:
#    zed will use the spare to handle the removed data device
# 7. Repeat the same tests again with a faulted spare device:
#    the removed data device should be removed
#
# NOTE: the use of 'block_device_wait' throughout the test helps avoid race
# conditions caused by mixing creation/removal events from partitioning the
# disk (zpool create) and events from physically removing it (remove_disk).
#
# NOTE: the test relies on ZED to transit state to removed on device removed
# event.  The ZED does receive a removal notification but only relies on it to
# activate a hot spare.  Additional work is planned to extend an existing ioctl
# interface to allow the ZED to transition the vdev in to a removed state.
#
verify_runnable "both"

if is_linux; then
	# Add one 512b scsi_debug device (4Kn would generate IO errors)
	# NOTE: must be larger than other "file" vdevs and minimum SPA devsize:
	# add 32m of fudge
	load_scsi_debug $(($MINVDEVSIZE/1024/1024+32)) 1 1 1 '512b'
else
	log_unsupported "scsi debug module unsupported"
fi

function cleanup
{
	destroy_pool $TESTPOOL
	rm -f $filedev1
	rm -f $filedev2
	rm -f $filedev3
	rm -f $sparedev
	unload_scsi_debug
}

log_assert "ZED detects physically removed devices"

log_onexit cleanup

filedev1="$TEST_BASE_DIR/file-vdev-1"
filedev2="$TEST_BASE_DIR/file-vdev-2"
filedev3="$TEST_BASE_DIR/file-vdev-3"
sparedev="$TEST_BASE_DIR/file-vdev-spare"
removedev=$(get_debug_device)

typeset poolconfs=(
    "mirror $filedev1 $removedev"
    "raidz3 $filedev1 $filedev2 $filedev3 $removedev"
    "mirror $filedev1 $filedev2 special mirror $filedev3 $removedev"
)

log_must truncate -s $MINVDEVSIZE $filedev1
log_must truncate -s $MINVDEVSIZE $filedev2
log_must truncate -s $MINVDEVSIZE $filedev3
log_must truncate -s $MINVDEVSIZE $sparedev

for conf in "${poolconfs[@]}"
do
	# 1. Create a pool
	log_must zpool create -f $TESTPOOL $conf
	block_device_wait ${DEV_DSKDIR}/${removedev}

	mntpnt=$(get_prop mountpoint /$TESTPOOL)

	# 2. Simulate physical removal of one device
	remove_disk $removedev
	log_must mkfile 1m $mntpnt/file
	sync_pool $TESTPOOL

	# 3. Verify the device is removed.
	log_must wait_vdev_state $TESTPOOL $removedev "REMOVED"

	# 4. Reattach the device
	insert_disk $removedev

	# 5. Verify the device is onlined
	log_must wait_vdev_state $TESTPOOL $removedev "ONLINE"

	# cleanup
	destroy_pool $TESTPOOL
	log_must parted "${DEV_DSKDIR}/${removedev}" -s -- mklabel msdos
	block_device_wait ${DEV_DSKDIR}/${removedev}
done

# 6. Repeat the same tests with a spare device: zed will use the spare to handle
#    the removed data device
for conf in "${poolconfs[@]}"
do
	# 1. Create a pool with a spare
	log_must zpool create -f $TESTPOOL $conf
	block_device_wait ${DEV_DSKDIR}/${removedev}
	log_must zpool add $TESTPOOL spare $sparedev

	mntpnt=$(get_prop mountpoint /$TESTPOOL)

	# 2. Simulate physical removal of one device
	remove_disk $removedev
	log_must mkfile 1m $mntpnt/file
	sync_pool $TESTPOOL

	# 3. Verify the device is handled by the spare.
	log_must wait_hotspare_state $TESTPOOL $sparedev "INUSE"
	log_must wait_vdev_state $TESTPOOL $removedev "REMOVED"

	# 4. Reattach the device
	insert_disk $removedev

	# 5. Verify the device is onlined
	log_must wait_vdev_state $TESTPOOL $removedev "ONLINE"

	# cleanup
	destroy_pool $TESTPOOL
	log_must parted "${DEV_DSKDIR}/${removedev}" -s -- mklabel msdos
	block_device_wait ${DEV_DSKDIR}/${removedev}
done

# 7. Repeat the same tests again with a faulted spare device: zed should offline
#    the removed data device if no spare is available
for conf in "${poolconfs[@]}"
do
	# 1. Create a pool with a spare
	log_must zpool create -f $TESTPOOL $conf
	block_device_wait ${DEV_DSKDIR}/${removedev}
	log_must zpool add $TESTPOOL spare $sparedev

	mntpnt=$(get_prop mountpoint /$TESTPOOL)

	# 2. Fault the spare device making it unavailable
	log_must zpool offline -f $TESTPOOL $sparedev
	log_must wait_hotspare_state $TESTPOOL $sparedev "FAULTED"

	# 3. Simulate physical removal of one device
	remove_disk $removedev
	log_must mkfile 1m $mntpnt/file
	sync_pool $TESTPOOL

	# 4. Verify the device is removed
	log_must wait_vdev_state $TESTPOOL $removedev "REMOVED"

	# 5. Reattach the device
	insert_disk $removedev

	# 6. Verify the device is onlined
	log_must wait_vdev_state $TESTPOOL $removedev "ONLINE"

	# cleanup
	destroy_pool $TESTPOOL
	log_must parted "${DEV_DSKDIR}/${removedev}" -s -- mklabel msdos
	block_device_wait ${DEV_DSKDIR}/${removedev}
done

log_pass "ZED detects physically removed devices"
