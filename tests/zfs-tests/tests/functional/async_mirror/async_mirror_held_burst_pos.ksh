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
#	A burst of frees on an async mirror with a healthy lagging child
#	does not slow the writes of the pool for seconds.  The child passes
#	the txg of the frees at once, the held space then waits only for
#	its release, and the release runs at once, in a txg of its own.  It
#	does not wait for the next txg that the pool syncs for other
#	reasons, while the write throttle delays each write.
#
# STRATEGY:
#	1. Set the txg timeout to 30 seconds, so that the pool syncs no
#	   txg on its own during the test.
#	2. Create a pool with a two-way mirror and turn the async mirror on.
#	   Write a file of 200 MiB, more than a quarter of the pool, and
#	   wait until the lagging child has caught up.
#	3. Measure the time of 60 small writes.
#	4. Remove the file and sync the pool once: the txg of the frees.
#	5. Measure the time of 60 small writes again.  With the release in
#	   the next txg of the pool, each of them would take the most delay
#	   of the write throttle, 100 ms: 6 seconds more.  Verify that they
#	   took at most 2 seconds more than before.
#	6. Verify that the held space came back with no other sync of the
#	   pool, that the copy did not fail, and the pool.
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
typeset -i before after

log_must async_mirror_set_tunable32 TXG_TIMEOUT 30
async_mirror_create_pool
log_must zfs set compression=off $TESTPOOL
async_mirror_enable

async_mirror_write /$TESTPOOL/big 1600
sync_pool $TESTPOOL
log_must wait_async_mirror_idle
before=$(timed_writes /$TESTPOOL/before) || log_fail "Cannot write"
sync_pool $TESTPOOL
log_must wait_async_mirror_idle

log_must rm /$TESTPOOL/big
sync_pool $TESTPOOL
after=$(timed_writes /$TESTPOOL/after) || log_fail "Cannot write"
log_note "60 writes: $before s before the frees, $after s after them"
log_must test $after -le $(( before + 2 ))

log_must wait_async_mirror_stat held_bytes -lt $(( 16 * mib )) 10
log_must test "$(get_async_mirror_stat failed)" -eq 0
log_must wait_async_mirror_idle
log_must verify_pool $TESTPOOL

log_pass "A burst of frees on a healthy lagging child does not slow" \
    "writes for seconds."
