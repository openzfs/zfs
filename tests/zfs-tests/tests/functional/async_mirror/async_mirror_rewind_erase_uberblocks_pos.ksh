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
#	A rewind import to a txg below the newest txg of the lagging child
#	erases the uberblocks of the lagging child, turns the async mirror off, and
#	resilvers the child in full.  Those uberblocks belong to the history
#	that the rewind discards.
#
# STRATEGY:
#	1. Create a pool with a two-way mirror and turn the async mirror on.
#	2. Write files a, wait until the lagging child has caught up, and
#	   keep the txg of the pool.
#	3. Write files b, wait until the lagging child has caught up, and
#	   export the pool.
#	4. Import the pool with -T at the kept txg.
#	5. Verify that the async mirror is off, and that the lagging child has no
#	   uberblock above the kept txg from before the import.
#	6. Wait for the resilver.  Verify files a, that files b are gone,
#	   and that a scrub finds no errors.
#

verify_runnable "global"

log_onexit cleanup_async_mirror

async_mirror_create_pool
async_mirror_enable

async_mirror_write_files a 4
async_mirror_suspend_release 1
log_must wait_async_mirror_idle
typeset digests="$(async_mirror_digests a 4)"
typeset -i txg=$(get_async_mirror_pool_txg)

async_mirror_write_files b 4
log_must wait_async_mirror_idle
log_must zpool export $TESTPOOL

typeset -i newest=$(get_ring_max_txg $ASYNC_MIRROR_LAGGING)
log_note "rewind txg $txg, newest txg of the lagging child $newest"
log_must test $newest -gt $txg
log_must test $(count_ring_txgs $ASYNC_MIRROR_LAGGING $txg $newest) -gt 0

async_mirror_import_txg $txg
async_mirror_suspend_release 0
log_must test "$(get_async_mirror_stat active)" == 0
log_must test $(count_ring_txgs $ASYNC_MIRROR_LAGGING $txg $newest) -eq 0

log_must wait_async_mirror_resilvered
log_must test "$digests" == "$(async_mirror_digests a 4)"
log_mustnot test -e /$TESTPOOL/b.0
log_must verify_pool $TESTPOOL
log_must check_pool_status $TESTPOOL errors "No known data errors"

log_pass "A rewind erases the discarded uberblocks of the lagging child."
