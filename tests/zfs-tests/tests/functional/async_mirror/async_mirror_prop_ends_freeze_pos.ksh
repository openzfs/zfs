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
#	The property ends a freeze with no export.  The frozen child gets
#	writes again, its DTL covers every txg from its frozen txg, and the
#	resilver gives it the files written while it was frozen.
#
# STRATEGY:
#	1. Create a pool with a two-way mirror and turn the async mirror on.  Write
#	   files a and wait until the lagging child has caught up.
#	2. Damage every copy of the indirect block of a new file b on the
#	   leader before its copy, and wait until the lagging child freezes.
#	   Write files c.
#	3. Set the property of the lagging child to off.  Wait until the
#	   child is a normal child, for the resilver, and until the feature
#	   returns to enabled.
#	4. Export, and verify that the child has an uberblock newer than its
#	   frozen txg.  Import the child alone, and verify files a and c.
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
async_mirror_write_files c 2
sync_pool $TESTPOOL
typeset digests="$(async_mirror_digests a 2; async_mirror_digests c 2)"

log_must zpool set write_mode=sync $TESTPOOL $ASYNC_MIRROR_LAGGING
log_must wait_async_mirror_state off
log_must wait_async_mirror_resilvered
log_must wait_async_mirror_feature enabled

log_must zpool export $TESTPOOL
log_must test $(get_ring_max_txg $ASYNC_MIRROR_LAGGING) -gt $frozen
async_mirror_import_lagging
log_must test "$digests" == "$(async_mirror_digests a 2; \
    async_mirror_digests c 2)"
log_must zpool export $TESTPOOL
async_mirror_unhide_leader

log_pass "The property ends a freeze, and the child is resilvered."
