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
# Verify that per-vdev ZAPs persist when the pool is exported and imported.
#
# Strategy:
# 1. Create a pool with a disk.
# 2. Export the pool and re-import it.
# 3. Verify that the ZAPs aren't different.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/vdev_zaps/vdev_zaps.kshlib

log_assert "Per-vdev ZAPs persist across export/import."

DISK=${DISKS%% *}
log_must zpool create -f $TESTPOOL $DISK

# Make the pool.
conf="$TESTDIR/vz005"
log_must zdb -PC $TESTPOOL > $conf
assert_has_sentinel "$conf"
orig_top=$(get_top_vd_zap $DISK $conf)
orig_leaf=$(get_leaf_vd_zap $DISK $conf)
assert_zap_common $TESTPOOL $DISK "top" $orig_top
assert_zap_common $TESTPOOL $DISK "leaf" $orig_leaf
log_must zpool sync

# Export the pool.
log_must zpool export $TESTPOOL

# Import the pool.
log_must zpool import $TESTPOOL

# Verify that ZAPs persisted.
log_must zdb -PC $TESTPOOL > $conf

new_top=$(get_top_vd_zap $DISK $conf)
new_leaf=$(get_leaf_vd_zap $DISK $conf)

[[ "$new_top" -ne "$orig_top" ]] && log_fail "Top ZAP ($new_top) after "\
        "import does not match top ZAP before export ($orig_top)"
[[ "$new_leaf" -ne "$orig_leaf" ]] && log_fail "Leaf ZAP ($new_leaf) after "\
        "import does not match leaf ZAP before export ($orig_leaf)"

log_pass
