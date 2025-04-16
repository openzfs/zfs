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
# Copyright 2019, loli10K <ezomori.nozomu@gmail.com>. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/removal/removal.kshlib

#
# DESCRIPTION:
# Device removal cannot remove non-concrete vdevs
#
# STRATEGY:
# 1. Create a pool with removable devices
# 2. Remove a top-level device
# 3. Verify we can't remove the "indirect" vdev created by the first removal
#

verify_runnable "global"

function cleanup
{
	destroy_pool $TESTPOOL
	log_must rm -f $TEST_BASE_DIR/device-{1,2,3}
}

log_assert "Device removal should not be able to remove non-concrete vdevs"
log_onexit cleanup

# 1. Create a pool with removable devices
truncate -s $MINVDEVSIZE $TEST_BASE_DIR/device-{1,2,3}
zpool create $TESTPOOL $TEST_BASE_DIR/device-{1,2,3}

# 2. Remove a top-level device
log_must zpool remove $TESTPOOL $TEST_BASE_DIR/device-1
log_must wait_for_removal $TESTPOOL

# 3. Verify we can't remove the "indirect" vdev created by the first removal
INDIRECT_VDEV=$(zpool list -v -g $TESTPOOL | awk '{if ($2 == "-") { print $1; exit} }')
log_must test -n "$INDIRECT_VDEV"
log_mustnot zpool remove $TESTPOOL $INDIRECT_VDEV

log_pass "Device removal cannot remove non-concrete vdevs"
