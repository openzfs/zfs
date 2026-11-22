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
#	The copy defers a limited number of data blocks for one transaction
#	group.  When more blocks cannot be read, the lagging child freezes at
#	the transaction group before, and nothing of the damaged transaction
#	group is visible on it.  Without a limit, a large area that does not
#	read would defer blocks without a bound.
#
# STRATEGY:
#	1. Create a pool with a two-way mirror and turn the async mirror on.
#	   Write files a, and wait until the lagging child has caught up.
#	2. Allow 1 try after the first, and 2 deferred blocks for each
#	   transaction group.  With the copy stopped, write file b of 8
#	   blocks.  Export and import, so that the ARC does not hold b.  Inject
#	   read errors on the data of b that stay.
#	3. Restart the copy.  Verify that the lagging child freezes before the
#	   transaction group of b, and that no deferred block is left.
#	4. Verify that the pool still writes.  Clear the injection, export,
#	   and verify that the newest uberblock of the lagging child is the
#	   frozen transaction group.  Import the lagging child alone: files a
#	   are intact, and a scrub finds no error.
#

verify_runnable "global"

log_onexit cleanup_async_mirror

log_assert "Too many deferred data blocks freeze the lagging child."

async_mirror_create_pool
async_mirror_enable
async_mirror_write_files a 2
log_must wait_async_mirror_idle
typeset digests="$(async_mirror_digests a 2)"

log_must async_mirror_set_tunable32 ASYNC_MIRROR_READ_RETRIES 1
log_must async_mirror_set_tunable32 ASYNC_MIRROR_DEFER_MAX 2

async_mirror_suspend_copy 1
typeset -i before=$(get_async_mirror_stat applied_txg)
async_mirror_write /$TESTPOOL/b 8
sync_pool $TESTPOOL
typeset -i txg_b=$(get_async_mirror_pool_txg)
log_must zpool export $TESTPOOL
async_mirror_import
log_must test "$(get_async_mirror_stat pending_txgs)" -gt 0
log_must zinject -a -t data -e io -T read /$TESTPOOL/b

async_mirror_suspend_copy 0
log_must wait_async_mirror_frozen $(( 2 * ASYNC_MIRROR_TIMEOUT ))
log_must zinject -c all

typeset -i frozen=$(get_async_mirror_stat frozen_txg)
log_note "applied txg $before before b, b is in txg $txg_b, frozen at $frozen"
log_must test $frozen -ge $before
log_must test $frozen -lt $txg_b
log_must test "$(get_async_mirror_stat applied_txg)" == $frozen
log_must test "$(get_async_mirror_prop state)" == frozen
log_must test "$(get_async_mirror_stat data_deferred)" == 0

# The pool goes on, and the frozen child gets nothing of it.
async_mirror_write_files c 1
log_must test $(get_async_mirror_pool_txg) -gt $frozen
log_must zpool export $TESTPOOL
log_must test $(get_ring_max_txg $ASYNC_MIRROR_LAGGING) -eq $frozen

async_mirror_import_lagging
log_must test "$digests" == "$(async_mirror_digests a 2)"
log_must zpool scrub -w $TESTPOOL
log_must check_pool_status $TESTPOOL errors "No known data errors"
log_must test "$(async_mirror_cksum_errors $ASYNC_MIRROR_LAGGING)" == 0
log_must zpool export $TESTPOOL
async_mirror_unhide_leader

log_pass "Too many deferred data blocks freeze the lagging child."
