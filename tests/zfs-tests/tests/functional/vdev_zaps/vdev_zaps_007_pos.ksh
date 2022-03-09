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
# Verify that ZAPs are handled properly during mirror pool splitting.
#
# Strategy:
# 1. Create a pool with a two-way mirror.
# 2. Split the pool.
# 3. Verify that the ZAPs in the old pool persisted.
# 4. Import the new pool.
# 5. Verify that the ZAPs in the new pool persisted.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/vdev_zaps/vdev_zaps.kshlib

DISK_ARR=($DISKS)
POOL2=${TESTPOOL}2
log_must zpool create -f $TESTPOOL mirror ${DISK_ARR[0]} ${DISK_ARR[1]}

log_assert "Per-vdev ZAPs persist correctly on the original pool after split."
conf="$TESTDIR/vz007"
log_must eval "zdb -PC $TESTPOOL > $conf"

assert_has_sentinel "$conf"
orig_top=$(get_top_vd_zap "type: 'mirror'" $conf)
orig_leaf0=$(get_leaf_vd_zap ${DISK_ARR[0]} $conf)
orig_leaf1=$(get_leaf_vd_zap ${DISK_ARR[1]} $conf)
assert_zap_common $TESTPOOL "type: 'mirror'" "top" $orig_top
assert_zap_common $TESTPOOL ${DISK_ARR[0]} "leaf" $orig_leaf0
assert_zap_common $TESTPOOL ${DISK_ARR[1]} "leaf" $orig_leaf1

log_must zpool split $TESTPOOL $POOL2 ${DISK_ARR[1]}

# Make sure old pool's ZAPs are consistent.
log_must eval "zdb -PC $TESTPOOL > $conf"
new_leaf0=$(get_leaf_vd_zap ${DISK_ARR[0]} $conf)
new_top_s0=$(get_top_vd_zap ${DISK_ARR[0]} $conf)

[[ "$new_leaf0" -ne "$orig_leaf0" ]] && log_fail "Leaf ZAP in original pool "\
        "didn't persist (expected $orig_leaf0, got $new_leaf0)"
[[ "$new_top_s0" -ne "$orig_top" ]] && log_fail "Top ZAP in original pool "\
        "didn't persist (expected $orig_top, got $new_top_s0)"

log_assert "Per-vdev ZAPs persist on the new pool after import."

# Import the split pool.
log_must zpool import $POOL2
log_must eval "zdb -PC $POOL2 > $conf"

new_leaf1=$(get_leaf_vd_zap ${DISK_ARR[1]} $conf)
new_top_s1=$(get_top_vd_zap ${DISK_ARR[1]} $conf)
[[ "$new_leaf1" -ne "$orig_leaf1" ]] && log_fail "Leaf ZAP in new pool "\
        "didn't persist (expected $orig_leaf1, got $new_leaf1)"
[[ "$new_top_s1" -ne "$orig_top" ]] && log_fail "Top ZAP in new pool "\
        "didn't persist (expected $orig_top, got $new_top_s1)"

log_pass
