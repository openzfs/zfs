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
#	Held space that the lagging child has not passed does not delay the
#	writes of the pool.  No release can free that space before the child
#	has the txg of the frees, so a delay would get no space back: it
#	would only make the writes wait for a slow lagging child.
#
# STRATEGY:
#	1. Create a pool with a two-way mirror and turn the async mirror on.
#	   Write a file of 200 MiB, more than a quarter of the pool, and
#	   wait until the lagging child has caught up.
#	2. Measure the time of 60 small writes.
#	3. Delay each I/O of the lagging child by 5 seconds.  Remove the
#	   file and sync the pool: the frees are held, and the child needs
#	   many seconds to pass their txg.
#	4. Measure the time of 60 small writes again.  With a delay for
#	   all of the held space, each of them would take the most delay
#	   of the write throttle, 100 ms: 6 seconds more.  Verify that they
#	   took at most 2 seconds more than before, and that the space was
#	   still held when they ended.
#	5. Remove the delay of the child.  Verify that the child catches
#	   up, that the held space comes back, and the pool.
#

verify_runnable "global"

log_onexit cleanup_async_mirror

#
# Write 60 blocks of 8 KiB to a file, each with a write call of its own,
# and print the seconds that it took.
#
function timed_writes # file
{
	typeset -i i
	typeset -i start=$SECONDS

	for (( i = 0; i < 60; i++ )); do
		dd if=/dev/urandom of=$1 bs=8k count=1 seek=$i conv=notrunc \
		    2>/dev/null || return 1
	done
	echo $(( SECONDS - start ))
}

ASYNC_MIRROR_VDEV_SIZE=512m
typeset -i mib=$(( 1024 * 1024 ))
typeset -i before after held

async_mirror_create_pool
log_must zfs set compression=off $TESTPOOL
async_mirror_enable
# This test holds much space on purpose: no give-up for it.
log_must async_mirror_set_tunable32 ASYNC_MIRROR_HELD_LIMIT_PERCENT 0
log_must async_mirror_set_tunable32 ASYNC_MIRROR_HELD_FREE_PERCENT 0

async_mirror_write /$TESTPOOL/big 1600
sync_pool $TESTPOOL
log_must wait_async_mirror_idle
before=$(timed_writes /$TESTPOOL/before) || log_fail "Cannot write"
sync_pool $TESTPOOL
log_must wait_async_mirror_idle

log_must zinject -d $(get_leaf_guid $ASYNC_MIRROR_LAGGING) -D 5000:1 $TESTPOOL
log_must rm /$TESTPOOL/big
sync_pool $TESTPOOL
after=$(timed_writes /$TESTPOOL/after) || log_fail "Cannot write"
held=$(get_async_mirror_stat held_bytes)
log_note "60 writes: $before s before the frees, $after s after them;" \
    "$held bytes held"
log_must test $held -ge $(( 150 * mib ))
log_must test $after -le $(( before + 2 ))

log_must zinject -c all
log_must wait_async_mirror_idle
sync_pool $TESTPOOL
log_must wait_async_mirror_stat held_bytes -lt $(( 16 * mib ))
log_must test "$(get_async_mirror_stat failed)" -eq 0
log_must verify_pool $TESTPOOL

log_pass "Held space that the lagging child has not passed does not" \
    "delay writes."
