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
# Verify that per-vdev ZAPs are properly transferred on attach/detach.
#
# Strategy:
# 1. Create a pool with one disk. Verify that it has a top and leaf ZAP.
# 2. Attach a disk.
# 3. Verify that top-level and leaf-level ZAPs were transferred properly.
# 4. Verify that the newly-attached disk has a leaf ZAP.
# 5. Detach the original disk.
# 6. Verify that top-level and leaf-level ZAPs were transferred properly.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/vdev_zaps/vdev_zaps.kshlib

log_assert "Per-vdev ZAPs are transferred properly on attach/detach"

DISK=${DISKS%% *}
log_must zpool create -f $TESTPOOL $DISK

# Make the pool.
conf="$TESTDIR/vz004"
log_must eval "zdb -PC $TESTPOOL > $conf"
assert_has_sentinel "$conf"
orig_top=$(get_top_vd_zap $DISK $conf)
orig_leaf=$(get_leaf_vd_zap $DISK $conf)
assert_zap_common $TESTPOOL $DISK "top" $orig_top

#
# Attach a disk.
#

read -r _ disk2 _ <<<"$DISKS"
log_must zpool attach $TESTPOOL $DISK $disk2
log_must zpool wait -t resilver $TESTPOOL
log_must eval "zdb -PC $TESTPOOL > $conf"

# Ensure top-level ZAP was transferred successfully.
new_top=$(get_top_vd_zap "type: 'mirror'" $conf)
if [[ "$new_top" -ne "$orig_top" ]]; then
        log_fail "Top-level ZAP wasn't transferred successfully on attach."
fi

# Ensure leaf ZAP of original disk was transferred successfully.
new_leaf=$(get_leaf_vd_zap $DISK $conf)
if [[ "$new_leaf" -ne "$orig_leaf" ]]; then
        log_fail "$DISK used to have leaf-level ZAP $orig_leaf, now has "\
                "$new_leaf"
fi
# Ensure original disk no longer has top-level ZAP.
dsk1_top=$(get_top_vd_zap $DISK $conf)
[[ -n "$dsk1_top" ]] && log_fail "$DISK has top-level ZAP, but is only leaf."

# Ensure attached disk got a leaf-level ZAP but not a top-level ZAP.
dsk2_top=$(get_top_vd_zap $disk2 $conf)
dsk2_leaf=$(get_leaf_vd_zap $disk2 $conf)
[[ -n "$dsk2_top" ]] && log_fail "Attached disk $disk2 has top ZAP."
[[ -z "$dsk2_leaf" ]] && log_fail "Attached disk $disk2 has no leaf ZAP."

#
# Detach original disk.
#

log_must zpool detach $TESTPOOL $DISK
log_must eval "zdb -PC $TESTPOOL > $conf"

final_top=$(get_top_vd_zap $disk2 $conf)
final_leaf=$(get_leaf_vd_zap $disk2 $conf)
# Make sure top ZAP was successfully transferred.
[[ "$final_top" -ne "$orig_top" ]] && log_fail "Lost top-level ZAP when "\
        "promoting $disk2 (expected $orig_top, found $final_top)"

# Make sure leaf ZAP was successfully transferred.
[[ "$final_leaf" -ne "$dsk2_leaf" ]] && log_fail "$disk2 lost its leaf ZAP "\
        "on promotion (expected $dsk2_leaf, got $final_leaf)"

log_pass
