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
# Copyright (c) 2015, 2016 by Delphix. All rights reserved.
#

#
# Description:
# Verify that top-level per-vdev ZAPs are created for added devices
#
# Strategy:
# 1. Create a pool with one disk.
# 2. Add a disk.
# 3. Verify its ZAPs were created.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/vdev_zaps/vdev_zaps.kshlib

DISK_ARR=($DISKS)
DISK=${DISK_ARR[0]}
log_must zpool create -f $TESTPOOL $DISK

log_assert "Per-vdev ZAPs are created for added vdevs."

log_must zpool add -f $TESTPOOL ${DISK_ARR[1]}
conf="$TESTDIR/vz006"
log_must eval "zdb -PC $TESTPOOL > $conf"

assert_has_sentinel "$conf"
assert_root_zap $TESTPOOL "$conf"
orig_top=$(get_top_vd_zap ${DISK_ARR[1]} $conf)
assert_zap_common $TESTPOOL ${DISK_ARR[1]} "top" $orig_top
assert_leaf_zap $TESTPOOL ${DISK_ARR[1]} "$conf"

log_pass
