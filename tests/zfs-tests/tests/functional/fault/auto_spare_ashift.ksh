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
. $STF_SUITE/include/math.shlib
. $STF_SUITE/tests/functional/fault/fault.cfg

#
# DESCRIPTION:
# Testing Fault Management Agent ZED Logic - Automated Auto-Spare Test when
# drive is faulted and a custom ashift value needs to be provided to replace it.
#
# STRATEGY:
# 1. Create a pool from 512b devices and set "ashift" pool property accordingly
# 2. Add one 512e spare device (4Kn would generate IO errors on replace)
# 3. Inject IO errors with a zinject error handler
# 4. Start a scrub
# 5. Verify the ZED kicks in the hot spare and expected pool/device status
# 6. Clear the fault
# 7. Verify the hot spare is available and expected pool/device status
#

verify_runnable "both"

function cleanup
{
	log_must zinject -c all
	destroy_pool $TESTPOOL
	unload_scsi_debug
	rm -f $SAFE_DEVICE $FAIL_DEVICE
}

log_assert "ZED should replace a device using the configured ashift property"
log_onexit cleanup

# Clear events from previous runs
zed_events_drain

SAFE_DEVICE="$TEST_BASE_DIR/safe-dev"
FAIL_DEVICE="$TEST_BASE_DIR/fail-dev"

# 1. Create a pool from 512b devices and set "ashift" pool property accordingly
for vdev in $SAFE_DEVICE $FAIL_DEVICE; do
	truncate -s $MINVDEVSIZE $vdev
done
log_must zpool create -f $TESTPOOL mirror $SAFE_DEVICE $FAIL_DEVICE
# NOTE: file VDEVs should be added as 512b devices, verify this "just in case"
for vdev in $SAFE_DEVICE $FAIL_DEVICE; do
	verify_eq "9" "$(zdb -e -l $vdev | awk '/ashift: /{print $2}')" "ashift"
done
log_must zpool set ashift=9 $TESTPOOL

# 2. Add one 512e spare device (4Kn would generate IO errors on replace)
# NOTE: must be larger than the existing 512b devices, add 32m of fudge
load_scsi_debug $(($MINVDEVSIZE/1024/1024+32)) $SDHOSTS $SDTGTS $SDLUNS '512e'
SPARE_DEVICE=$(get_debug_device)
log_must_busy zpool add $TESTPOOL spare $SPARE_DEVICE

# 3. Inject IO errors with a zinject error handler
log_must zinject -d $FAIL_DEVICE -e io -T all -f 100 $TESTPOOL

# 4. Start a scrub
log_must zpool scrub $TESTPOOL

# 5. Verify the ZED kicks in a hot spare and expected pool/device status
log_note "Wait for ZED to auto-spare"
log_must wait_vdev_state $TESTPOOL $FAIL_DEVICE "FAULTED" 60
log_must wait_vdev_state $TESTPOOL $SPARE_DEVICE "ONLINE" 60
log_must wait_hotspare_state $TESTPOOL $SPARE_DEVICE "INUSE"
log_must check_state $TESTPOOL "" "DEGRADED"

# 6. Clear the fault
log_must zinject -c all
log_must zpool clear $TESTPOOL $FAIL_DEVICE

# 7. Verify the hot spare is available and expected pool/device status
log_must wait_vdev_state $TESTPOOL $FAIL_DEVICE "ONLINE" 60
log_must wait_hotspare_state $TESTPOOL $SPARE_DEVICE "AVAIL"
log_must is_pool_resilvered $TESTPOOL
log_must check_state $TESTPOOL "" "ONLINE"

log_pass "ZED successfully replaces a device using the configured ashift property"
