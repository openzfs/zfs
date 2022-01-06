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
# Copyright 2016 Nexenta Systems, Inc.
# Copyright (c) 2019 by Lawrence Livermore National Security, LLC.
#

. $STF_SUITE/tests/functional/cli_root/zpool_labelclear/labelclear.cfg

# DESCRIPTION:
#    Check that `zpool labelclear` can clear labels on removed devices.
#
# STRATEGY:
# 1. Create a pool with primary, log, spare and cache devices.
# 2. Remove a top-level vdev, log, spare, and cache device.
# 3. Run `zpool labelclear` on the removed device.
# 4. Verify the label has been removed.
#

verify_runnable "global"

function cleanup
{
	poolexists $TESTPOOL && destroy_pool $TESTPOOL
	rm -f $DEVICE1 $DEVICE2 $DEVICE3 $DEVICE4 $DEVICE5
}

log_onexit cleanup
log_assert "zpool labelclear works for removed devices"

DEVICE1="$TEST_BASE_DIR/device-1"
DEVICE2="$TEST_BASE_DIR/device-2"
DEVICE3="$TEST_BASE_DIR/device-3"
DEVICE4="$TEST_BASE_DIR/device-4"
DEVICE5="$TEST_BASE_DIR/device-5"

log_must truncate -s $((SPA_MINDEVSIZE * 8)) $DEVICE1
log_must truncate -s $SPA_MINDEVSIZE $DEVICE2 $DEVICE3 $DEVICE4 $DEVICE5

log_must zpool create -f $TESTPOOL $DEVICE1 $DEVICE2 \
    log $DEVICE3 cache $DEVICE4 spare $DEVICE5
sync_all_pools

# Remove each type of vdev and verify the label can be cleared.
for dev in $DEVICE5 $DEVICE4 $DEVICE3 $DEVICE2; do
	log_must zpool remove $TESTPOOL $dev
	sync_pool $TESTPOOL true
	log_must zpool labelclear $dev
	log_mustnot zdb -lq $dev
done

log_pass "zpool labelclear works for removed devices"
