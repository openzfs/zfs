#!/bin/ksh
# SPDX-License-Identifier: CDDL-1.0
#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# https://opensource.org/license/CDDL-1.0.
#

#
# Description:
# Verify that a metadata block that can't be allocated in the special
# class or the normal class is ganged, instead of falling back between
# the two classes forever (openzfs/zfs#19256).
#
# Strategy:
# 1. Create a pool with a special vdev.
# 2. Force every allocation of 1500 bytes or more to fail, as if both
#    classes were too fragmented for it.
# 3. Create enough files that the meta-dnode's blocks are larger than
#    that, and sync.  The sync must finish, and the blocks must be gangs.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/gang_blocks/gang_blocks.kshlib

log_assert "Metadata that fails in the special and normal classes is ganged"

preamble
log_onexit cleanup

read -r normal special rest <<< "$DISKS"
log_must zpool create -f -o ashift=9 $TESTPOOL $normal special $special
log_must zfs create -o recordsize=512 $TESTPOOL/$TESTFS
mountpoint=$(get_prop mountpoint $TESTPOOL/$TESTFS)

set_tunable64 METASLAB_FORCE_GANGING 1500
set_tunable32 METASLAB_FORCE_GANGING_PCT 100

for i in $(seq 1 80); do
	dd if=/dev/urandom of=$mountpoint/f$i bs=512 count=1 2>/dev/null || \
	    log_fail "dd failed"
done

# Without the fix, the meta-dnode's blocks bounce between the special and
# normal classes and the sync never finishes.
zpool sync $TESTPOOL &
typeset pid=$!
typeset -i waited=0
while kill -0 $pid 2>/dev/null; do
	(( waited >= 120 )) && log_fail "zpool sync hung for ${waited}s"
	sleep 1
	(( waited += 1 ))
done
log_must wait $pid

obj_0_gangs=$(get_object_info $TESTPOOL/$TESTFS 0 L0 | grep G)
[[ -n "$obj_0_gangs" ]] || log_fail "meta-dnode blocks were not ganged"

log_must zdb -b $TESTPOOL

log_pass "Metadata that fails in the special and normal classes is ganged"
