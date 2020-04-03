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
# Copyright (c) 2017 by Intel Corporation. All rights reserved.
# Copyright 2017, loli10K <ezomori.nozomu@gmail.com>. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/fault/fault.cfg

#
# DESCRIPTION:
# Testing Fault Management Agent ZED Logic - Automated Auto-Spare Test when
# multiple drives are faulted.
#
# STRATEGY:
# 1. Create a pool with two hot spares
# 2. Inject IO ERRORS with a zinject error handler on the first device
# 3. Start a scrub
# 4. Verify the ZED kicks in a hot spare and expected pool/device status
# 5. Inject IO ERRORS on a second device
# 6. Start a scrub
# 7. Verify the ZED kicks in a second hot spare
# 8. Clear the fault on both devices
# 9. Verify the hot spares are available and expected pool/device status
# 10. Rinse and repeat, this time faulting both devices at the same time
#

verify_runnable "both"

function cleanup
{
	log_must zinject -c all
	destroy_pool $TESTPOOL
	rm -f $DATA_DEVS $SPARE_DEVS
}

log_assert "ZED should be able to handle multiple faulted devices"
log_onexit cleanup

# Events not supported on FreeBSD
if ! is_freebsd; then
	# Clear events from previous runs
	zed_events_drain
fi

FAULT_DEV1="$TEST_BASE_DIR/fault-dev1"
FAULT_DEV2="$TEST_BASE_DIR/fault-dev2"
SAFE_DEV1="$TEST_BASE_DIR/safe-dev1"
SAFE_DEV2="$TEST_BASE_DIR/safe-dev2"
DATA_DEVS="$FAULT_DEV1 $FAULT_DEV2 $SAFE_DEV1 $SAFE_DEV2"
SPARE_DEV1="$TEST_BASE_DIR/spare-dev1"
SPARE_DEV2="$TEST_BASE_DIR/spare-dev2"
SPARE_DEVS="$SPARE_DEV1 $SPARE_DEV2"

for type in "mirror" "raidz" "raidz2" "raidz3"; do
	# 1. Create a pool with two hot spares
	truncate -s $SPA_MINDEVSIZE $DATA_DEVS $SPARE_DEVS
	log_must zpool create -f $TESTPOOL $type $DATA_DEVS spare $SPARE_DEVS

	# 2. Inject IO ERRORS with a zinject error handler on the first device
	log_must zinject -d $FAULT_DEV1 -e io -T all -f 100 $TESTPOOL

	# 3. Start a scrub
	log_must zpool scrub $TESTPOOL

	# 4. Verify the ZED kicks in a hot spare and expected pool/device status
	log_note "Wait for ZED to auto-spare"
	log_must wait_vdev_state $TESTPOOL $FAULT_DEV1 "FAULTED" 60
	log_must wait_vdev_state $TESTPOOL $SPARE_DEV1 "ONLINE" 60
	log_must wait_hotspare_state $TESTPOOL $SPARE_DEV1 "INUSE"
	log_must check_state $TESTPOOL "" "DEGRADED"

	# 5. Inject IO ERRORS on a second device
	log_must zinject -d $FAULT_DEV2 -e io -T all -f 100 $TESTPOOL

	# 6. Start a scrub
	while is_pool_scrubbing $TESTPOOL || is_pool_resilvering $TESTPOOL; do
		sleep 1
	done
	log_must zpool scrub $TESTPOOL

	# 7. Verify the ZED kicks in a second hot spare
	log_note "Wait for ZED to auto-spare"
	log_must wait_vdev_state $TESTPOOL $FAULT_DEV2 "FAULTED" 60
	log_must wait_vdev_state $TESTPOOL $SPARE_DEV2 "ONLINE" 60
	log_must wait_hotspare_state $TESTPOOL $SPARE_DEV2 "INUSE"
	log_must check_state $TESTPOOL "" "DEGRADED"

	# 8. Clear the fault on both devices
	log_must zinject -c all
	log_must zpool clear $TESTPOOL $FAULT_DEV1
	log_must zpool clear $TESTPOOL $FAULT_DEV2

	# 9. Verify the hot spares are available and expected pool/device status
	log_must wait_vdev_state $TESTPOOL $FAULT_DEV1 "ONLINE" 60
	log_must wait_vdev_state $TESTPOOL $FAULT_DEV2 "ONLINE" 60
	log_must wait_hotspare_state $TESTPOOL $SPARE_DEV1 "AVAIL"
	log_must wait_hotspare_state $TESTPOOL $SPARE_DEV2 "AVAIL"
	log_must check_state $TESTPOOL "" "ONLINE"

	# Cleanup
	cleanup
done

# Rinse and repeat, this time faulting both devices at the same time
# NOTE: "raidz" is excluded since it cannot survive 2 faulted devices
# NOTE: "mirror" is a 4-way mirror here and should survive this test
for type in "mirror" "raidz2" "raidz3"; do
	# 1. Create a pool with two hot spares
	truncate -s $SPA_MINDEVSIZE $DATA_DEVS $SPARE_DEVS
	log_must zpool create -f $TESTPOOL $type $DATA_DEVS spare $SPARE_DEVS

	# 2. Inject IO ERRORS with a zinject error handler on two devices
	log_must eval "zinject -d $FAULT_DEV1 -e io -T all -f 100 $TESTPOOL &"
	log_must eval "zinject -d $FAULT_DEV2 -e io -T all -f 100 $TESTPOOL &"

	# 3. Start a scrub
	log_must zpool scrub $TESTPOOL

	# 4. Verify the ZED kicks in two hot spares and expected pool/device status
	log_note "Wait for ZED to auto-spare"
	log_must wait_vdev_state $TESTPOOL $FAULT_DEV1 "FAULTED" 60
	log_must wait_vdev_state $TESTPOOL $FAULT_DEV2 "FAULTED" 60
	log_must wait_vdev_state $TESTPOOL $SPARE_DEV1 "ONLINE" 60
	log_must wait_vdev_state $TESTPOOL $SPARE_DEV2 "ONLINE" 60
	log_must wait_hotspare_state $TESTPOOL $SPARE_DEV1 "INUSE"
	log_must wait_hotspare_state $TESTPOOL $SPARE_DEV2 "INUSE"
	log_must check_state $TESTPOOL "" "DEGRADED"

	# 5. Clear the fault on both devices
	log_must zinject -c all
	log_must zpool clear $TESTPOOL $FAULT_DEV1
	log_must zpool clear $TESTPOOL $FAULT_DEV2

	# Cleanup
	cleanup
done

log_pass "ZED successfully handles multiple faulted devices"
