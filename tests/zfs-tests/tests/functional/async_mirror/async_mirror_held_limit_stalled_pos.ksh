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
#	A lagging child that froze for the held space stays frozen until an
#	administrator starts a resilver. Pool writes do not wait for it.
#
# STRATEGY:
#	1. Create a pool with a two-way mirror and turn the async mirror on.
#	   Write a file of 40 MiB and wait until the lagging child has
#	   caught up.
#	2. Set the hard limit to 10% of the pool.  Stop the copy, delay each
#	   I/O of the lagging child by 1.5 seconds, remove the file and
#	   sync the pool until the child is frozen.
#	3. While the child stays slow, write 10 blocks with fsync and sync
#	   the pool three times. Verify that each round of 10 writes took at
#	   most 10 seconds: with a
#	   wait for the child it would take 15 seconds or more.
#	4. Verify that the child is still frozen at the same txg, that its
#	   write_mode is still behind, and that no resilver has started.
#

verify_runnable "global"

log_onexit cleanup_async_mirror
log_assert "A lagging child that stays slow stays frozen, and writes do" \
    "not wait for it."

typeset -i i n start took worst=0 frozen

async_mirror_create_pool
log_must zfs set compression=off $TESTPOOL
async_mirror_enable

async_mirror_write /$TESTPOOL/big 320
sync_pool $TESTPOOL
log_must wait_async_mirror_idle

async_mirror_freeze_at_held_limit /$TESTPOOL/big
frozen=$(get_async_mirror_stat frozen_txg)

for (( n = 0; n < 3; n++ )); do
	start=$SECONDS
	for (( i = 0; i < 10; i++ )); do
		log_must timeout 60 dd if=/dev/urandom of=/$TESTPOOL/w.$i \
		    bs=128k count=1 conv=fsync
	done
	took=$(( SECONDS - start ))
	(( took > worst )) && worst=$took
	sync_pool $TESTPOOL
	sleep 1
done
log_note "the slowest round of 10 writes took $worst s"
log_must test $worst -le 10

log_must test "$(get_async_mirror_stat frozen)" -eq 1
log_must test "$(get_async_mirror_stat held_limit)" -eq 1
log_must test "$(get_async_mirror_stat frozen_txg)" -eq $frozen
log_must test "$(get_async_mirror_prop state)" == frozen
log_must test "$(get_async_mirror_prop)" == behind
log_mustnot eval "zpool status $TESTPOOL | grep -q 'scan:'"
log_must test $(get_ring_max_txg $ASYNC_MIRROR_LAGGING) -eq $frozen

log_pass "A lagging child that stays slow stays frozen, and writes do" \
    "not wait for it."
