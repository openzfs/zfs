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
#	A block pointer that the copy refuses freezes the lagging child at
#	the transaction group before, and nothing of the refused transaction
#	group becomes visible on the child.  The copy refuses a block pointer
#	that points to a range that its transaction group did not allocate.
#	A healthy pool has no such block pointer, so the test asks for the
#	refusal with a tunable.
#
# STRATEGY:
#	1. Create a pool with a two-way mirror and turn the async mirror on.
#	   Write files a, and wait until the lagging child has caught up.
#	2. Stop the copy and write file b.  Make the copy refuse the first
#	   block pointer of each transaction group, and restart the copy.
#	3. Verify that the lagging child freezes at a transaction group
#	   before that of b.  Verify the async_mirror_refused event and its
#	   payload, and the async_mirror_frozen event.
#	4. Verify that the pool still writes.  Export, and verify that the
#	   newest uberblock of the lagging child is the frozen transaction
#	   group.  Import, and verify that the child is still frozen there.
#	5. Import the lagging child alone.  Verify files a, and that a scrub
#	   finds no error.
#

verify_runnable "global"

typeset event_file=$TEST_BASE_DIR/async_mirror_refused_events.$$

function cleanup
{
	[[ -e $ASYNC_MIRROR_HIDDEN ]] && async_mirror_unhide_leader
	cleanup_async_mirror
	rm -f $event_file
}

log_onexit cleanup

log_assert "A refused block pointer freezes the lagging child."

async_mirror_create_pool
async_mirror_enable
async_mirror_write_files a 2
log_must wait_async_mirror_idle
typeset digests="$(async_mirror_digests a 2)"

async_mirror_suspend_copy 1
typeset -i before=$(get_async_mirror_stat applied_txg)
async_mirror_write /$TESTPOOL/b 16
sync_pool $TESTPOOL
typeset -i txg_b=$(get_async_mirror_pool_txg)
log_must test "$(get_async_mirror_stat pending_txgs)" -gt 0

log_must zpool events -c
log_must async_mirror_set_tunable32 ASYNC_MIRROR_INJECT_REFUSE 1
async_mirror_suspend_copy 0
log_must wait_async_mirror_frozen
log_must async_mirror_set_tunable32 ASYNC_MIRROR_INJECT_REFUSE 0

typeset -i frozen=$(get_async_mirror_stat frozen_txg)
log_note "applied txg $before before b, b is in txg $txg_b, frozen at $frozen"
log_must test $frozen -ge $before
log_must test $frozen -lt $txg_b
log_must test "$(get_async_mirror_stat applied_txg)" == $frozen
log_must test "$(get_async_mirror_prop state)" == frozen

log_must eval "zpool events -v > $event_file"
typeset -i refused=$(grep -c 'sysevent\.fs\.zfs\.async_mirror_refused' \
    $event_file)
log_note "$refused async_mirror_refused events"
log_must test $refused -ge 1
log_must grep -q 'async_mirror_objset' $event_file
log_must grep -q 'async_mirror_offset' $event_file
log_must grep -q 'async_mirror_size' $event_file
log_must grep -q 'async_mirror_reason' $event_file
log_must grep -q 'the test suite asked for it' $event_file
log_must grep -q 'sysevent\.fs\.zfs\.async_mirror_frozen' $event_file

# The pool goes on, and the frozen child gets nothing of it.
async_mirror_write_files c 1
log_must test $(get_async_mirror_pool_txg) -gt $frozen
log_must zpool export $TESTPOOL
log_must test $(get_ring_max_txg $ASYNC_MIRROR_LAGGING) -eq $frozen

async_mirror_import
log_must test "$(get_async_mirror_stat frozen)" == 1
log_must test "$(get_async_mirror_stat frozen_txg)" == $frozen
log_must zpool export $TESTPOOL
log_must test $(get_ring_max_txg $ASYNC_MIRROR_LAGGING) -eq $frozen

async_mirror_import_lagging
log_must test "$digests" == "$(async_mirror_digests a 2)"
log_must zpool scrub -w $TESTPOOL
log_must check_pool_status $TESTPOOL errors "No known data errors"
log_must test "$(async_mirror_cksum_errors $ASYNC_MIRROR_LAGGING)" == 0
log_must zpool export $TESTPOOL
async_mirror_unhide_leader

log_pass "A refused block pointer freezes the lagging child."
