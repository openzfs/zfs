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

#
# DESCRIPTION:
#	On a pool that is nearly full, the async mirror does not give up on
#	a healthy lagging child that is some seconds behind.  Each overwrite
#	frees blocks that the child holds for those seconds, so the pool is
#	short of space with held space that waits for the child.  That is no
#	reason to freeze a child that makes progress: the space comes back
#	when the child has passed the txg.
#
# STRATEGY:
#	1. Create a pool with a two-way mirror and turn the async mirror on.
#	   Set the free space limit to 5% of the pool, about 24 MiB, and
#	   turn the other limits off.
#	2. Write a file "over" of 8 MiB, and fill the pool until about 28
#	   MiB are available: just above the limit.
#	3. Make the lagging child slow with real I/O: 30 ms for each I/O,
#	   one at a time.  On a pool that is nearly full, ZFS cuts an
#	   overwrite of 8 MiB into about 10 txgs, and the child writes each
#	   copy of their metadata too: some hundred I/Os.  The child then
#	   needs more than 10 seconds for them, well below the wait of the
#	   give-up of 60 seconds, and makes progress all the time.
#	4. Three times: write "over" again and sync the pool.  The pool is
#	   now short of space, and 8 MiB of held space wait for the child.
#	   Sync the pool again after 6 seconds, and verify that the child
#	   is not frozen.  Wait until the child has caught up.
#	5. Verify that the child was more than 6 seconds behind, and that
#	   the async mirror never gave up.
#

verify_runnable "global"

function cleanup
{
	cleanup_async_mirror
}

log_onexit cleanup
log_assert "The async mirror does not give up on a healthy lagging child" \
    "that is some seconds behind on a nearly full pool."

ASYNC_MIRROR_VDEV_SIZE=512m
typeset -i mib=$(( 1024 * 1024 ))
typeset -i i size avail fill lag

async_mirror_create_pool
log_must zfs set compression=off $TESTPOOL
async_mirror_enable

log_must async_mirror_set_tunable32 ASYNC_MIRROR_HELD_LIMIT_PERCENT 0
log_must async_mirror_set_tunable32 ASYNC_MIRROR_HELD_FREE_PERCENT 5
log_must async_mirror_set_tunable32 ASYNC_MIRROR_STALL_SECS 0

size=$(zpool get -Hp -o value size $TESTPOOL)
async_mirror_write /$TESTPOOL/over 64
sync_pool $TESTPOOL
avail=$(zfs get -Hp -o value available $TESTPOOL)
(( fill = avail / mib - 28 ))
log_must test $fill -gt 0
async_mirror_write /$TESTPOOL/fill $(( fill * 8 ))
sync_pool $TESTPOOL
log_must wait_async_mirror_idle
sync_pool $TESTPOOL
avail=$(zfs get -Hp -o value available $TESTPOOL)
log_note "pool size $size, $avail available; the limit is about" \
    "$(( size / 20 ))"
# Above the limit now, and below it after an overwrite of 8 MiB.
log_must test $avail -ge $(( size / 20 ))
log_must test $avail -lt $(( size / 20 + 7 * mib ))

#
# With 200 ms for each I/O the child needed 15 seconds for one of these
# txgs, and more than two minutes for all of them: that child is too slow,
# and the async mirror gave up on it after its wait, as it must.
#
async_mirror_stall_child 30
for (( i = 0; i < 3; i++ )); do
	log_must dd if=/dev/urandom of=/$TESTPOOL/over bs=128k count=64 \
	    conv=notrunc
	sync_pool $TESTPOOL
	log_must test "$(get_async_mirror_stat held_bytes)" -ge \
	    $(( 7 * mib ))
	sleep 6
	# The clock of the pool space is looked at here too.
	sync_pool $TESTPOOL
	log_must test "$(get_async_mirror_stat frozen)" -eq 0
	log_must test "$(get_async_mirror_stat pending_txgs)" -gt 0
	log_must wait_async_mirror_idle
	log_must test "$(get_async_mirror_stat frozen)" -eq 0
done
log_must zinject -c all

# The premise of the test: the child was behind for longer than the wait.
lag=$(get_async_mirror_stat max_lag_ms)
log_note "the lagging child was up to $lag ms behind"
log_must test $lag -ge 6000
log_must test "$(get_async_mirror_stat giveups)" -eq 0
log_must test "$(get_async_mirror_stat frozen)" -eq 0

log_pass "The async mirror does not give up on a healthy lagging child" \
    "that is some seconds behind on a nearly full pool."
