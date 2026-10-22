#!/bin/ksh -p
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
# Copyright (c) 2026 Tigerblue77
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#	Two "zpool set allocating=on" run at the same time on the same vdev
#	activate it once, so that a later "allocating=off" stops allocations
#	on it.
#
# STRATEGY:
#	1. Create a pool with two file vdevs.
#	2. Many times: set allocating=off on the second vdev, then run two
#	   "zpool set allocating=on" on it concurrently.
#	3. Set allocating=off again, write data, and check that the second
#	   vdev did not get any of it.
#

verify_runnable "global"

typeset POOL=alloc_race_pool
typeset VDEV0=$TEST_BASE_DIR/vdev_race0.$$
typeset VDEV1=$TEST_BASE_DIR/vdev_race1.$$

function cleanup
{
	poolexists $POOL && destroy_pool $POOL
	rm -f $VDEV0 $VDEV1
}

log_onexit cleanup

log_assert "concurrent 'zpool set allocating=on' activate a vdev once"

log_must truncate -s $MINVDEVSIZE $VDEV0 $VDEV1
log_must zpool create -O compression=off $POOL $VDEV0 $VDEV1

for i in {1..50}; do
	log_must zpool set allocating=off $POOL $VDEV1
	zpool set allocating=on $POOL $VDEV1 &
	zpool set allocating=on $POOL $VDEV1 &
	wait
	log_must test "$(zpool get -H -o value allocating $POOL $VDEV1)" = "on"
done

log_must zpool set allocating=off $POOL $VDEV1
sync_pool $POOL
typeset before=$(get_vdev_prop allocated $POOL $VDEV1)
log_must dd if=/dev/urandom of=/$POOL/file bs=1M count=16
sync_pool $POOL
typeset after=$(get_vdev_prop allocated $POOL $VDEV1)
log_note "allocated on $VDEV1 with allocating=off: $before -> $after"
[[ $before == +([0-9]) && $after == +([0-9]) ]] ||
    log_fail "allocated is not a number: '$before' '$after'"
log_must test "$after" -le "$before"

log_pass "concurrent 'zpool set allocating=on' activate a vdev once"
