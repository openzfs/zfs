#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
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
. $STF_SUITE/include/math.shlib
. $STF_SUITE/tests/functional/fault/fault.cfg

#
# DESCRIPTION:
# Spare devices (both files and disks) can be shared among different ZFS pools.
#
# STRATEGY:
# 1. Create two pools
# 2. Add the same spare device to different pools
# 3. Inject IO errors with a zinject error handler
# 4. Start a scrub
# 5. Verify the ZED kicks in a hot spare and check pool/device status
# 6. Clear the fault
# 7. Verify the hot spare is available and check pool/device status
#

verify_runnable "both"

if is_linux; then
	# Add one 512b spare device (4Kn would generate IO errors on replace)
	# NOTE: must be larger than other "file" vdevs and minimum SPA devsize:
	# add 32m of fudge
	load_scsi_debug $(($MINVDEVSIZE/1024/1024+32)) 1 1 1 '512b'
else
	log_unsupported "scsi debug module unsupported"
fi

function cleanup
{
	log_must zinject -c all
	destroy_pool $TESTPOOL
	destroy_pool $TESTPOOL1
	unload_scsi_debug
	rm -f $SAFE_FILEDEVPOOL1 $SAFE_FILEDEVPOOL2 $FAIL_FILEDEVPOOL1 \
	    $FAIL_FILEDEVPOOL2 $SPARE_FILEDEV
}

log_assert "Spare devices can be shared among different ZFS pools"
log_onexit cleanup

# Clear events from previous runs
zed_events_drain

SAFE_FILEDEVPOOL1="$TEST_BASE_DIR/file-safe-dev1"
FAIL_FILEDEVPOOL1="$TEST_BASE_DIR/file-fail-dev1"
SAFE_FILEDEVPOOL2="$TEST_BASE_DIR/file-safe-dev2"
FAIL_FILEDEVPOOL2="$TEST_BASE_DIR/file-fail-dev2"
SPARE_FILEDEV="$TEST_BASE_DIR/file-spare-dev"
SPARE_DISKDEV="$(get_debug_device)"

log_must truncate -s $MINVDEVSIZE $SAFE_FILEDEVPOOL1 $SAFE_FILEDEVPOOL2 $FAIL_FILEDEVPOOL1 $FAIL_FILEDEVPOOL2 $SPARE_FILEDEV

for spare in $SPARE_FILEDEV $SPARE_DISKDEV; do
	# 1. Create two pools
	log_must zpool create -f $TESTPOOL mirror $SAFE_FILEDEVPOOL1 $FAIL_FILEDEVPOOL1
	log_must zpool create -f $TESTPOOL1 mirror $SAFE_FILEDEVPOOL2 $FAIL_FILEDEVPOOL2

	# 2. Add the same spare device to different pools
	log_must_busy zpool add $TESTPOOL spare $spare
	log_must_busy zpool add $TESTPOOL1 spare $spare
	log_must wait_hotspare_state $TESTPOOL $spare "AVAIL"
	log_must wait_hotspare_state $TESTPOOL1 $spare "AVAIL"

	# 3. Inject IO errors with a zinject error handler
	log_must zinject -d $FAIL_FILEDEVPOOL1 -e io -T all -f 100 $TESTPOOL
	log_must zinject -d $FAIL_FILEDEVPOOL2 -e io -T all -f 100 $TESTPOOL1

	# 4. Start a scrub
	log_must zpool scrub $TESTPOOL
	log_must zpool scrub $TESTPOOL1

	# 5. Verify the ZED kicks in a hot spare and check pool/device status
	log_note "Wait for ZED to auto-spare"
	log_must wait_vdev_state $TESTPOOL $FAIL_FILEDEVPOOL1 "FAULTED" 60
	log_must wait_vdev_state $TESTPOOL $spare "ONLINE" 60
	log_must wait_hotspare_state $TESTPOOL $spare "INUSE"
	log_must check_state $TESTPOOL "" "DEGRADED"

	# 6. Clear the fault
	log_must zinject -c all
	log_must zpool clear $TESTPOOL $FAIL_FILEDEVPOOL1

	# 7. Verify the hot spare is available and check pool/device status
	log_must wait_vdev_state $TESTPOOL $FAIL_FILEDEVPOOL1 "ONLINE" 60
	log_must wait_hotspare_state $TESTPOOL $spare "AVAIL"
	log_must is_pool_resilvered $TESTPOOL
	log_must check_state $TESTPOOL "" "ONLINE"

	# Cleanup
	destroy_pool $TESTPOOL
	destroy_pool $TESTPOOL1
done

log_pass "Spare devices can be shared among different ZFS pools"
