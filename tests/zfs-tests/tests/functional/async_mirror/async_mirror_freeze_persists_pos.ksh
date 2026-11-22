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
#	A frozen lagging child stays frozen across an export and an import,
#	and gets no write.  An import after the property has turned the async mirror
#	off ends the freeze: the child is resilvered, and the feature
#	returns to enabled.  This is the import after a crash between the
#	property change and the txg that ends the freeze.
#
# STRATEGY:
#	1. Create a pool with a two-way mirror and turn the async mirror on.  Write
#	   files a and wait until the lagging child has caught up.
#	2. Damage every copy of the indirect block of a new file b on the
#	   leader before its copy, and wait until the lagging child freezes.
#	3. Export and import.  Verify that the child is still frozen at the
#	   same txg, and that the feature is active.  Write files c, export,
#	   and verify that the newest uberblock of the child is the frozen
#	   txg.
#	4. Import, and wait until the records are released.  Stop the
#	   release, which records the end of the freeze, and set the
#	   property of the lagging child to off.  Wait until the copy is
#	   failed, and export.
#	5. Restart the release and import.  Verify that the freeze has
#	   ended, wait for the resilver, verify that the feature returns to
#	   enabled, and that files a and c are intact.
#

verify_runnable "global"

log_onexit cleanup_async_mirror

async_mirror_create_pool
async_mirror_enable
async_mirror_write_files a 2
log_must wait_async_mirror_idle

async_mirror_damage_backlog /$TESTPOOL/b 1 0
log_must wait_async_mirror_frozen
typeset -i frozen=$(get_async_mirror_stat frozen_txg)
log_must zpool export $TESTPOOL

async_mirror_import
log_must test "$(get_async_mirror_stat frozen)" == 1
log_must test "$(get_async_mirror_stat frozen_txg)" == $frozen
log_must test "$(get_async_mirror_stat active)" == 1
log_must test "$(get_async_mirror_feature)" == "active"
async_mirror_write_files c 2
typeset digests="$(async_mirror_digests a 2; async_mirror_digests c 2)"
log_must zpool export $TESTPOOL
log_must test $(get_ring_max_txg $ASYNC_MIRROR_LAGGING) -eq $frozen

# The operator ends the freeze, and the pool stops before it is recorded.
async_mirror_import
log_must wait_async_mirror_released
async_mirror_suspend_release 1
log_must zpool set write_mode=sync $TESTPOOL $ASYNC_MIRROR_LAGGING
log_must wait_async_mirror_state failed
log_must zpool export $TESTPOOL
async_mirror_suspend_release 0

async_mirror_import
log_must test "$(get_async_mirror_stat frozen)" == 0
log_must test "$(get_async_mirror_stat active)" == 0
log_must wait_async_mirror_resilvered
log_must wait_async_mirror_feature enabled
log_must test "$digests" == "$(async_mirror_digests a 2; \
    async_mirror_digests c 2)"
log_must test $(get_ring_max_txg $ASYNC_MIRROR_LAGGING) -gt $frozen

log_pass "A freeze persists until the operator ends it."
