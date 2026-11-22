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
# Copyright (c) 2026, John White.
#

. $STF_SUITE/tests/functional/async_mirror/async_mirror.kshlib

#
# DESCRIPTION:
#	While much held space is ready for its release, the release runs in a
#	txg of its own.  The txg of a release holds new space of its own: the
#	blocks of the MOS that it wrote again.  A forced release that the
#	entry limit ended, and that left no less space held than before, must
#	not be followed by another forced release.  The next forced release
#	would be as futile, and the pool would sync txgs without end, each
#	one writing an uberblock.
#
# STRATEGY:
#	1. Set the txg timeout to 5 minutes, so that the pool syncs no txg on
#	   its own, and limit one release to one SM_FREE entry: 8 KiB, far
#	   less than the MOS blocks that one txg of a release writes again.
#	2. Create a pool with a two-way mirror and turn the async mirror on.
#	3. Stop the release.  Write a file of 8 KiB blocks, and write every
#	   second block again: 64 MiB in about 8000 held ranges, more than a
#	   quarter of the held space limit of the pool.  Wait until the
#	   lagging child has caught up, so that all of it is ready.
#	4. Restart the release, write one small file, and sync the pool.  The
#	   copy of that txg wakes the release, which joins the next txg.  Sync
#	   the pool three more times, 2 seconds apart, so that each queued
#	   release gets its txg, and the forced release follows it.
#	5. Count the txgs that the pool syncs in the next 20 seconds, with no
#	   write and no sync from the test.  Verify that there are at most
#	   3, and that nearly all of the held space is still held, so that
#	   the release has not simply ended.
#	6. Remove the limit.  Write one small file and sync the pool.  Verify
#	   that the held space comes back, and the pool.
#
# The signal is the txg of the newest uberblock of the pool.  With no txg
# timeout and no write, only a forced release makes the pool sync a txg,
# so the number of txgs is the number of forced releases.  On the code
# before the fix each forced release is followed by another, as long as
# the held space is high.  The pool then syncs a txg every few
# milliseconds, and hundreds in 20 seconds.  After the fix there is one
# forced release, which is futile, and then none until the next txg that
# the pool syncs for another reason.
#

verify_runnable "global"

log_onexit cleanup_async_mirror

ASYNC_MIRROR_VDEV_SIZE=512m
typeset -i mib=$(( 1024 * 1024 ))
typeset -i txg0 txg1 held0 held1

log_must async_mirror_set_tunable32 TXG_TIMEOUT 300
log_must async_mirror_set_tunable64 ASYNC_MIRROR_RELEASE_LIMIT 1
async_mirror_create_pool
log_must zfs set recordsize=8k $TESTPOOL
log_must zfs set compression=off $TESTPOOL
async_mirror_enable

typeset -i size=$(get_pool_prop size $TESTPOOL)
typeset -i quarter=$(( size / 100 * 25 / 4 ))

async_mirror_suspend_release 1
async_mirror_hold_ranges /$TESTPOOL/ranges 16384
held0=$(get_async_mirror_stat held_bytes)
log_note "held space before the release: $held0 bytes," \
    "a quarter of the limit is $quarter bytes"
log_must test $held0 -ge $(( quarter + 8 * mib ))

async_mirror_suspend_release 0
async_mirror_write /$TESTPOOL/kick 1
sync_pool $TESTPOOL
log_must wait_async_mirror_idle

# The copy of the kick txg wakes the release, which joins the next txg that
# the pool syncs, and the first one that it frees is capped by the limit.
# A forced release follows it in a txg of its own.  Each of these syncs
# gives a queued release its txg; the pool syncs none on its own.
typeset -i i
for (( i = 0; i < 3; i++ )); do
	sync_pool $TESTPOOL
	sleep 2
done

txg0=$(get_async_mirror_pool_txg)
sleep 20
txg1=$(get_async_mirror_pool_txg)
held1=$(get_async_mirror_stat held_bytes)
log_note "txgs in 20 seconds with no write: $(( txg1 - txg0 ))," \
    "held space $held1 bytes"
log_must test $(( txg1 - txg0 )) -le 3
log_must test $held1 -ge $(( held0 - 2 * mib ))
log_must test "$(get_async_mirror_stat failed)" -eq 0

# With no limit the release finishes, and the held space comes back.
log_must async_mirror_set_tunable64 ASYNC_MIRROR_RELEASE_LIMIT 0
async_mirror_write /$TESTPOOL/kick2 1
sync_pool $TESTPOOL
log_must wait_async_mirror_stat held_bytes -lt $(( held0 - 16 * mib )) 90
log_must test "$(get_async_mirror_stat failed)" -eq 0
log_must verify_pool $TESTPOOL

log_pass "A forced release that the entry limit ended, with no gain, is not" \
    "repeated."
