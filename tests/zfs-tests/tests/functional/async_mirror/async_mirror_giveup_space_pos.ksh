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
#	On a full pool, a small delete that a stalled lagging child holds is
#	all the space that the datasets can get.  The async mirror gives up
#	on the child for it, also when the pool syncs no txg after the
#	delete: the held space comes back, and a write succeeds.  The size
#	of the held space does not count. A healthy child on the same pool is
#	not frozen. The child stays frozen after space returns until an
#	administrator starts a normal resilver.
#
# STRATEGY:
#	1. Create a pool with a two-way mirror and turn the async mirror on.
#	   Set the hard limit of the held space to 90% of the pool, which
#	   the test never reaches, and the free space limit to 10%.
#	2. Write a file "small" of 8 MiB, and fill the pool until a write
#	   fails.  Wait until the lagging child has caught up, and verify
#	   that it is not frozen: it holds no space that waits for it.
#	3. Stop the copy, remove "small" and sync the pool once.  The space
#	   of "small" is held: far less than 10% of the pool.
#	4. Wait, with no sync of the pool, until the child is frozen.
#	   Verify the reason "pool space low" in the kstat and in the event.
#	5. Verify that the held space comes back, and that a file of 4 MiB
#	   can be written.
#	6. Restart the copy. The child stays frozen while the pool is short
#	   of space. Remove the fill file, set write_mode=sync, and wait for
#	   a normal resilver. Then set write_mode=behind again.
#

verify_runnable "global"

typeset event_file=$TEST_BASE_DIR/async_mirror_giveup_space_events.$$

function cleanup
{
	cleanup_async_mirror
	rm -f $event_file
}

log_onexit cleanup
log_assert "The async mirror gives up on a stalled lagging child when" \
    "the pool is full and the child holds a small delete."

ASYNC_MIRROR_VDEV_SIZE=512m
typeset -i mib=$(( 1024 * 1024 ))
typeset -i i size avail held after

async_mirror_create_pool
log_must zfs set compression=off $TESTPOOL
async_mirror_enable

log_must async_mirror_set_tunable32 ASYNC_MIRROR_HELD_LIMIT_PERCENT 90
log_must async_mirror_set_tunable32 ASYNC_MIRROR_HELD_FREE_PERCENT 10
log_must async_mirror_set_tunable32 ASYNC_MIRROR_STALL_SECS 0

size=$(zpool get -Hp -o value size $TESTPOOL)
async_mirror_write /$TESTPOOL/small 64
sync_pool $TESTPOOL
# Fill the pool: the write ends with ENOSPC.
log_mustnot dd if=/dev/urandom of=/$TESTPOOL/fill bs=128k
sync_pool $TESTPOOL
log_must wait_async_mirror_idle
sync_pool $TESTPOOL
avail=$(zfs get -Hp -o value available $TESTPOOL)
log_note "pool size $size, $avail available"
log_must test $avail -lt $(( 4 * mib ))

# A healthy child: the pool is full, but no held space waits for the child.
for (( i = 0; i < 8; i++ )); do
	sync_pool $TESTPOOL
	sleep 1
done
log_must test "$(get_async_mirror_stat frozen)" -eq 0
log_must test "$(get_async_mirror_stat giveups)" -eq 0

log_must zpool events -c
async_mirror_suspend_copy 1
log_must rm /$TESTPOOL/small
sync_pool $TESTPOOL
held=$(get_async_mirror_stat held_bytes)
log_note "$held bytes held after the free"
log_must test $held -ge $(( 7 * mib ))
# Far less than the free space limit, and than the hard limit.
log_must test $held -lt $(( size / 20 ))

# No txg is synced here: the pool gives up by its own clock.
for (( i = 0; i < 30; i++ )); do
	[[ $(get_async_mirror_stat frozen) == 1 ]] && break
	sleep 1
done
log_must test "$(get_async_mirror_stat frozen)" -eq 1
log_must test "$(get_async_mirror_stat held_limit)" -eq 1
# 2 is "pool space low"; see async_mirror_giveup_t.
log_must test "$(get_async_mirror_stat giveup_reason)" -eq 2
log_must eval "zpool events -v > $event_file"
log_must grep -q 'async_mirror_reason = "pool space low"' $event_file

for (( i = 0; i < ASYNC_MIRROR_TIMEOUT; i++ )); do
	after=$(zfs get -Hp -o value available $TESTPOOL)
	(( $(get_async_mirror_stat held_bytes) < mib &&
	    after >= avail + 6 * mib )) && break
	sync_pool $TESTPOOL
	sleep 1
done
log_note "available: $avail before the give-up, $after after it"
log_must test $after -ge $(( avail + 6 * mib ))
log_must timeout 60 file_write -o create -f /$TESTPOOL/again -b 131072 \
    -c 32 -d R
sync_pool $TESTPOOL

# The pool is still short of space: the child stays frozen.
async_mirror_suspend_copy 0
log_must test "$(get_async_mirror_stat frozen)" -eq 1

# With space again, the administrator starts a normal resilver.
log_must rm /$TESTPOOL/fill
sync_pool $TESTPOOL
log_must zpool set write_mode=sync $TESTPOOL $ASYNC_MIRROR_LAGGING
log_must wait_async_mirror_resilvered
log_must test "$(get_async_mirror_prop)" == sync
log_must zpool set write_mode=behind $TESTPOOL $ASYNC_MIRROR_LAGGING
log_must wait_async_mirror_state "behind|current"
log_must wait_async_mirror_idle
log_must verify_pool $TESTPOOL

log_pass "The async mirror gives up on a stalled lagging child when" \
    "the pool is full and the child holds a small delete."
