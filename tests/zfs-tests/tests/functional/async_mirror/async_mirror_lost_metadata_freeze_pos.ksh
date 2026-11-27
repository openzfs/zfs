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
#	A metadata block that is damaged on the leader in every copy before
#	the copy freezes the lagging child at the txg before: the newest
#	state that it holds in full.
#
# STRATEGY:
#	1. Create a pool with a two-way mirror and turn the async mirror on.  Write
#	   files a, and wait until the lagging child has caught up.
#	2. With the copy stopped, write file b and damage every copy of its
#	   indirect block on the leader.  Export, restart the copy, and
#	   import.
#	3. Verify that the lagging child freezes, and that the pool still
#	   writes past the frozen txg.
#	4. Export.  Verify that the newest uberblock of the lagging child is
#	   the frozen txg.  Import the lagging child alone: files a are
#	   intact, b, if it exists, reads without an error, and a scrub
#	   finds no error.
#

verify_runnable "global"

log_onexit cleanup_async_mirror

async_mirror_create_pool
async_mirror_enable
async_mirror_write_files a 2
log_must wait_async_mirror_idle
typeset digests="$(async_mirror_digests a 2)"
async_mirror_damage_backlog /$TESTPOOL/b 1 0
log_must wait_async_mirror_frozen
typeset -i frozen=$(get_async_mirror_stat frozen_txg)
log_note "frozen txg $frozen"
log_must test $frozen -gt 0
log_must test "$(get_async_mirror_stat applied_txg)" == $frozen
log_must test "$(get_async_mirror_stat failed)" == 1
log_must test "$(get_async_mirror_stat active)" == 1
async_mirror_write_files c 1
log_must test $(get_async_mirror_pool_txg) -gt $frozen
log_must zpool export $TESTPOOL
log_must test $(get_ring_max_txg $ASYNC_MIRROR_LAGGING) -eq $frozen

async_mirror_import_lagging
log_must test "$digests" == "$(async_mirror_digests a 2)"
#
# The writes of b can span more than one txg, and only the last one has
# the damaged indirect block.  So b can exist at the frozen txg, as it
# was then.  It must read without an error: nothing damaged reached the
# lagging child.
#
if [[ -e /$TESTPOOL/b ]]; then
	log_note "b exists at the frozen txg"
	log_must eval "cat /$TESTPOOL/b > /dev/null"
fi
log_must zpool scrub -w $TESTPOOL
log_must check_pool_status $TESTPOOL errors "No known data errors"
log_must zpool export $TESTPOOL
async_mirror_unhide_leader

log_pass "Lost metadata freezes the lagging child at its last clean txg."
