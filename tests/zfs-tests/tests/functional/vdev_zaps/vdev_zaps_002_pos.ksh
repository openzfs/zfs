#!/bin/ksh

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
# Copyright (c) 2015 by Delphix. All rights reserved.
#

#
# Description:
# Verify that per-vdev ZAPs are created with multiple vdevs.
#
# Strategy:
# 1. Create a pool with multiple disks.
# 2. Verify that each has both a top and leaf zap.
# 3. Verify that each of those ZAPs exists in the MOS.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/vdev_zaps/vdev_zaps.kshlib

log_assert "Per-vdev ZAPs are created on pool creation with many disks."

log_must zpool create -f $TESTPOOL $DISKS

conf="$TESTDIR/vz002"
log_must eval "zdb -PC $TESTPOOL > $conf"

assert_has_sentinel "$conf"
assert_root_zap $TESTPOOL "$conf"
for DISK in $DISKS; do
	assert_top_zap $TESTPOOL $DISK "$conf"
	assert_leaf_zap $TESTPOOL $DISK "$conf"
done

log_pass
