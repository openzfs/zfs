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
#	A large delete does not make the async mirror give up on a healthy
#	lagging child that is some seconds behind.  The held space passes
#	its hard limit, but the child passes the txg of the frees within
#	the wait, so the child is not frozen, and the space comes back by
#	the normal release.
#
# STRATEGY:
#	1. Create a pool with a two-way mirror of 1 GiB and turn the async
#	   mirror on.  Write a file of 128 MiB and wait until the lagging
#	   child has caught up.
#	2. Set the hard limit of the held space to 10% of the pool, and its
#	   wait to 30 seconds.  Delay each I/O of the lagging child by 100
#	   ms, so that the child needs about 10 seconds for 12 MiB.
#	3. Write 12 MiB and sync the pool.  Remove the large file and sync
#	   the pool: more than the limit is held, behind a txg that the
#	   child copies for about 10 seconds.
#	4. Sync the pool each second for 20 seconds, and verify each time
#	   that the child is not frozen.
#	5. Remove the delay.  Verify that the child catches up, that the
#	   async mirror did not give up, and that the held space is back.
#

verify_runnable "global"

log_onexit cleanup_async_mirror
log_assert "A large delete does not freeze a healthy lagging child that" \
    "is some seconds behind."

typeset -i mib=$(( 1024 * 1024 ))
typeset -i i held max_held=0

#
# The pool must have room for the 12 MiB in one txg.  ZFS counts the dirty
# data of a txg 24 times against the space that is left, and forces a txg
# when that is more.  On a pool of 128 MiB the write is cut into many txgs,
# the slow child holds the frees of each of them, and the write then fails
# with ENOSPC.
#
ASYNC_MIRROR_VDEV_SIZE=1g
async_mirror_create_pool
log_must zfs set compression=off $TESTPOOL
async_mirror_enable

async_mirror_write /$TESTPOOL/big 1024
sync_pool $TESTPOOL
log_must wait_async_mirror_idle

# The txg timeout was the wait of the give-up once: keep it short.
log_must async_mirror_set_tunable32 TXG_TIMEOUT 5
log_must async_mirror_set_tunable32 ASYNC_MIRROR_HELD_LIMIT_PERCENT 10
log_must async_mirror_set_tunable32 ASYNC_MIRROR_HELD_LIMIT_SECS 30
log_must async_mirror_set_tunable32 ASYNC_MIRROR_HELD_FREE_PERCENT 0
log_must zinject -d $(get_leaf_guid $ASYNC_MIRROR_LAGGING) -D 100:1 $TESTPOOL

async_mirror_write /$TESTPOOL/load 96
sync_pool $TESTPOOL
log_must rm /$TESTPOOL/big
sync_pool $TESTPOOL

for (( i = 0; i < 20; i++ )); do
	held=$(get_async_mirror_stat held_bytes)
	(( held > max_held )) && max_held=$held
	log_must test "$(get_async_mirror_stat frozen)" -eq 0
	sync_pool $TESTPOOL
	sleep 1
done
log_note "at most $max_held bytes were held"
# The limit is 10% of the pool, about 101 MiB.
log_must test $max_held -ge $(( 120 * mib ))

log_must zinject -c all
log_must wait_async_mirror_idle
sync_pool $TESTPOOL
log_must wait_async_mirror_stat held_bytes -lt $(( 8 * mib ))
log_must test "$(get_async_mirror_stat frozen)" -eq 0
log_must test "$(get_async_mirror_stat failed)" -eq 0
log_must test "$(get_async_mirror_stat giveups)" -eq 0
log_must test "$(get_async_mirror_prop state)" == current
log_must verify_pool $TESTPOOL

log_pass "A large delete does not freeze a healthy lagging child that" \
    "is some seconds behind."
