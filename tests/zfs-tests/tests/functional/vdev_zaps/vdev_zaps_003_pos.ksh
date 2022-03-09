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
# Verify that per-vdev ZAPs are created with multi-level vdev tree.
#
# Strategy:
# 1. Create a pool with a multi-disk mirror.
# 2. Verify that mirror has top ZAP but no leaf ZAP.
# 3. Verify that each disk has a leaf ZAP but no top ZAP.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/vdev_zaps/vdev_zaps.kshlib

log_assert "Per-vdev ZAPs are created on pool creation with multi-level vdev "\
        "trees."

log_must zpool create -f $TESTPOOL mirror $DISKS

conf="$TESTDIR/vz003"
log_must eval "zdb -PC $TESTPOOL > $conf"

assert_has_sentinel "$conf"
assert_top_zap $TESTPOOL "type: 'mirror'" "$conf"
for DISK in $DISKS; do
	assert_leaf_zap $TESTPOOL $DISK "$conf"
        top_zap=$(get_top_vd_zap $DISK "$conf")
        [[ -n "$top_zap" ]] && log_fail "Leaf vdev $DISK has top-level ZAP."
done

log_pass
