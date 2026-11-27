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
#	While much space is held, the release of the records does not wait
#	for the txg timeout.  A release that does not finish in one sync
#	task continues in txgs of its own, so the held space of a pool
#	with no writes comes back in seconds, not at one part of the
#	release for each txg timeout.
#
# STRATEGY:
#	1. Set the txg timeout to one minute, and limit one release sync task
#	   to 256 held ranges: 2 MiB.  The txg of a forced release writes MOS
#	   blocks again, and holds their old space.  A release that frees
#	   less than that is not forced again, so the limit must free much
#	   more than the MOS blocks of a txg, which are a few hundred KiB.
#	2. Create a pool with a two-way mirror and turn the async mirror on.
#	3. Stop the release.  Write a file of 8 KiB blocks, and write
#	   every second block again: 64 MiB in about 8000 held ranges,
#	   which is more than a quarter of the held space limit of the
#	   pool.  Wait until the lagging child has caught up.
#	4. Restart the release, and write one small file.  Its copy wakes the
#	   release, which joins the next txg.  Sync the pool once more, so
#	   that this first release runs.
#	5. With no more writes and no sync of the pool, verify that at
#	   least 16 MiB of the held space come back in 90 seconds.  That
#	   needs about 8 releases.  With one release for each txg timeout
#	   it would take 8 minutes, and 90 seconds free about 4 MiB.
#

verify_runnable "global"

log_onexit cleanup_async_mirror

ASYNC_MIRROR_VDEV_SIZE=512m
typeset -i mib=$(( 1024 * 1024 ))

log_must async_mirror_set_tunable32 TXG_TIMEOUT 60
log_must async_mirror_set_tunable64 ASYNC_MIRROR_RELEASE_LIMIT 256
async_mirror_create_pool
log_must zfs set recordsize=8k $TESTPOOL
log_must zfs set compression=off $TESTPOOL
async_mirror_enable

async_mirror_suspend_release 1
async_mirror_hold_ranges /$TESTPOOL/ranges 16384
typeset -i held=$(get_async_mirror_stat held_bytes)
log_note "held space before the release: $held bytes"
log_must test $held -ge $(( 60 * mib ))

async_mirror_suspend_release 0
async_mirror_write /$TESTPOOL/kick 1
sync_pool $TESTPOOL
log_must wait_async_mirror_idle
sleep 1
sync_pool $TESTPOOL

log_must wait_async_mirror_stat held_bytes -lt $(( held - 16 * mib )) 90
log_must test "$(get_async_mirror_stat failed)" -eq 0
log_must verify_pool $TESTPOOL

log_pass "The release does not wait for the txg timeout while much space" \
    "is held."
