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
#	The write throttle sees the held space of the async mirror that
#	waits for its release, and only that space.  When the held space that
#	the lagging child has passed is above its limit, each write of a
#	dataset is delayed, so that the datasets slow down while the release
#	gets the space back.  Held space that the child has not passed delays
#	no write: no release can free it, so a delay would only make the
#	writes wait for the child.  The writes are only delayed: they do not
#	wait for the copy or for the release, and the pool still syncs txgs.
#
# STRATEGY:
#	1. Set the limit of held space to 2 percent of the pool, a little
#	   more than 9 MiB.  Create a pool with a two-way mirror and turn
#	   the async mirror on.
#	2. Write a file of 8 KiB blocks and wait until the lagging child has
#	   caught up.  Measure the time of 80 small writes while no space is
#	   held.
#	3. Delay each I/O of the lagging child by 5 seconds.  Write half of
#	   the blocks of the file again: 20 MiB in about 2500 held ranges,
#	   twice the limit.  The child needs many seconds to pass their txg.
#	4. Measure the time of 80 small writes again.  The held space is not
#	   ready, so no write is delayed: verify that they took at most 2
#	   seconds more than before, that the child has not passed the txg of
#	   the frees, and that the space is held.  With a delay for all of the
#	   held space, each write would take the most delay of the write
#	   throttle, 100 ms: 8 seconds more.
#	5. Limit one release sync task to one held range, so that the held
#	   space stays for a while.  Remove the delay of the child, and wait
#	   until it has passed the txg of the frees: the held space is ready.
#	6. Measure the time of 80 small writes a third time.  Each write now
#	   takes the most delay of the write throttle, 100 ms, so the 80
#	   writes take at least 8 seconds.  Verify that they took at least 4
#	   seconds more than before.
#	7. Verify that the pool syncs and that the copy did not fail.
#	   Remove the limit of the release, and verify that the held space
#	   comes back and the pool is intact.
#

verify_runnable "global"

log_onexit cleanup_async_mirror

#
# Write 80 blocks of 8 KiB to a file, each with a write call of its own,
# and print the seconds that it took.
#
function timed_writes # file
{
	typeset -i i
	typeset -i start=$SECONDS

	for (( i = 0; i < 80; i++ )); do
		dd if=/dev/urandom of=$1 bs=8k count=1 seek=$i conv=notrunc \
		    2>/dev/null || return 1
	done
	echo $(( SECONDS - start ))
}

ASYNC_MIRROR_VDEV_SIZE=512m
typeset -i mib=$(( 1024 * 1024 ))
typeset -i blocks=5120
typeset src=$TEST_BASE_DIR/async_mirror_throttle.$$
typeset -i before slow ready

log_must async_mirror_set_tunable32 ASYNC_MIRROR_MAX_HELD_PERCENT 2
async_mirror_create_pool
log_must zfs set recordsize=8k $TESTPOOL
log_must zfs set compression=off $TESTPOOL
async_mirror_enable

typeset -i size=$(get_pool_prop size $TESTPOOL)
typeset -i limit=$(( size / 100 * 2 ))

log_must dd if=/dev/urandom of=$src bs=8k count=$blocks
log_must dd if=$src of=/$TESTPOOL/ranges bs=8k
sync_pool $TESTPOOL
log_must wait_async_mirror_idle
before=$(timed_writes /$TESTPOOL/before) || log_fail "Cannot write"
sync_pool $TESTPOOL
log_must wait_async_mirror_idle

# Held space that the slow child has not passed: no delay.
log_must zinject -d $(get_leaf_guid $ASYNC_MIRROR_LAGGING) -D 5000:1 $TESTPOOL
log_must stride_dd -i $src -o /$TESTPOOL/ranges -b 8192 \
    -c $(( blocks / 2 )) -s 2
log_must rm -f $src
sync_pool $TESTPOOL
log_must wait_async_mirror_stat pending_txgs -gt 0 10
typeset -i txg_frees=$(get_async_mirror_stat synced_txg)
slow=$(timed_writes /$TESTPOOL/slow) || log_fail "Cannot write"
typeset -i held=$(get_async_mirror_stat held_bytes)
log_note "held space: $held bytes, the limit is $limit bytes"
log_note "80 writes: $before s with no held space, $slow s with space" \
    "that the child has not passed"
log_must test $held -ge $(( limit + 4 * mib ))
log_must test $(get_async_mirror_stat applied_txg) -lt $txg_frees
log_must test $slow -le $(( before + 2 ))

# Ready held space above the limit: each write is delayed.
log_must async_mirror_set_tunable64 ASYNC_MIRROR_RELEASE_LIMIT 1
log_must zinject -c all
log_must wait_async_mirror_applied $txg_frees 240
ready=$(timed_writes /$TESTPOOL/ready) || log_fail "Cannot write"
log_note "80 writes: $before s with no held space, $ready s with it"
log_must test $ready -ge $(( before + 4 ))

# The delay is on the writes only: the pool syncs, and the copy goes on.
log_must zpool sync -l $TESTPOOL
log_must test "$(get_async_mirror_stat failed)" -eq 0
log_must async_mirror_set_tunable64 ASYNC_MIRROR_RELEASE_LIMIT 0
async_mirror_write /$TESTPOOL/kick 1
sync_pool $TESTPOOL
log_must wait_async_mirror_stat held_bytes -lt $(( 8 * mib ))
log_must wait_async_mirror_idle
log_must verify_pool $TESTPOOL

log_pass "The write throttle delays writes for held space that the child" \
    "has passed, and not for space that it has not passed."
