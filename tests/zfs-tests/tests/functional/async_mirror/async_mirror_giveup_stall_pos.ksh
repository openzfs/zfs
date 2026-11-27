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
#	Writes never wait without end for a lagging child that is stalled.
#	When the backlog of the copy is at its limit, where writes wait, and
#	the copy makes no progress, the async mirror gives up on the child
#	and freezes it, and the writes go on.  The stall is real I/O: each
#	I/O of the child takes longer than the stall time, and the worker
#	of the copy waits in it.  The copy is not suspended.
#
# STRATEGY:
#	1. Create a pool with a two-way mirror and turn the async mirror on.
#	   Write two files and wait until the lagging child has caught up.
#	2. Let writes wait for the copy when more than one txg waits for
#	   it, set the stall time to 3 seconds, and turn the limits of the
#	   held space and the probes off.  Delay each I/O of the lagging
#	   child by 6 seconds.
#	3. Write 12 files, each with fsync and a time limit of 30 seconds,
#	   and sync the pool after each one.  Verify that no write reaches
#	   its time limit, and that the child freezes with the reason "copy
#	   stalled".
#	4. Verify the event, the text of 'zpool status', that a write with
#	   a time limit ends, and that the records are released.
#	5. Remove the delay and export the pool.  Verify that the lagging
#	   child is still at its frozen txg.  Import it alone and verify
#	   that it holds the first two files, and a scrub.
#

verify_runnable "global"

typeset event_file=$TEST_BASE_DIR/async_mirror_giveup_stall_events.$$

function cleanup
{
	zinject -c all > /dev/null 2>&1
	[[ -e $ASYNC_MIRROR_HIDDEN ]] && poolexists $TESTPOOL && \
	    zpool export $TESTPOOL
	[[ -e $ASYNC_MIRROR_HIDDEN ]] && async_mirror_unhide_leader
	cleanup_async_mirror
	rm -f $event_file
}

log_onexit cleanup
log_assert "Writes do not wait without end for a stalled lagging child:" \
    "the async mirror gives up on it."

typeset -i frozen start took
typeset status digests

async_mirror_create_pool
log_must zfs set compression=off $TESTPOOL
async_mirror_enable

async_mirror_write_files keep 2
sync_pool $TESTPOOL
log_must wait_async_mirror_idle
digests="$(async_mirror_digests keep 2)"

log_must zpool events -c
start=$SECONDS
async_mirror_freeze_by_stall /$TESTPOOL/w
took=$(( SECONDS - start ))
log_note "the writes and the freeze took $took s"
frozen=$(get_async_mirror_stat frozen_txg)
log_must test $frozen -eq $(get_async_mirror_stat applied_txg)
log_must test "$(get_async_mirror_stat giveups)" -eq 1

log_must eval "zpool events -v > $event_file"
log_must grep -q 'sysevent\.fs\.zfs\.async_mirror_frozen' $event_file
log_must grep -q 'async_mirror_reason = "copy stalled"' $event_file
log_mustnot grep -q 'sysevent\.fs\.zfs\.async_mirror_failed' $event_file

status=$(zpool status $TESTPOOL)
log_note "$status"
echo "$status" | grep -q 'gave up on the lagging child (copy stalled)' ||
	log_fail "The status does not say that the pool gave up on the child"
echo "$status" | grep -q \
	'not protected: gave up (copy stalled), the child needs a resilver' ||
	log_fail "The async mirror line does not show the stall"

# The child is still stalled, and a write does not wait for it.
log_must timeout 30 dd if=/dev/urandom of=/$TESTPOOL/during bs=128k \
    count=8 conv=fsync
log_must wait_async_mirror_released
log_must test "$(get_async_mirror_stat pending_txgs)" -eq 0
log_must test "$(get_async_mirror_prop state)" == frozen

# The lagging child alone is the pool of its frozen txg.
log_must zinject -c all
log_must zpool export $TESTPOOL
log_must test $(get_ring_max_txg $ASYNC_MIRROR_LAGGING) -eq $frozen
async_mirror_import_lagging
log_must test "$digests" == "$(async_mirror_digests keep 2)"
log_mustnot test -e /$TESTPOOL/during
async_mirror_check_clean $ASYNC_MIRROR_LAGGING
log_must zpool export $TESTPOOL
async_mirror_unhide_leader

log_pass "Writes do not wait without end for a stalled lagging child:" \
    "the async mirror gives up on it."
