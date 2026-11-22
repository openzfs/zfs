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
#	While a child of the mirror is missing, while a fork is open, and
#	while a child needs a resilver, the feature stays active, also after
#	the records are gone.  Software without the feature can then import
#	the pool read-only only, so it cannot write labels without a
#	lineage.  The feature returns to enabled only when both children
#	are present and the same, and the async mirror is off.
#
# STRATEGY:
#	1. Create a pool with a two-way mirror and turn the async mirror on.
#	2. Write files a, wait until the lagging child has caught up, and
#	   export the pool.
#	3. Hide the lagging child, import the leader alone, and write files
#	   b.  Verify that the feature does not return to enabled.  Export
#	   and import again, and verify it again.
#	4. Bring the lagging child back and import the pool.  Wait for the
#	   resilver.  Verify that the feature is still active: the fork is
#	   open until the next import.
#	5. Turn the async mirror off.  Verify that the feature returns to enabled,
#	   and that files a and b are intact.
#

verify_runnable "global"

log_onexit cleanup_async_mirror

async_mirror_create_pool
async_mirror_enable

async_mirror_write_files a 2
log_must wait_async_mirror_idle
log_must zpool export $TESTPOOL

async_mirror_import_leader
log_must test "$(get_async_mirror_lineage lineage_parent)" != 0
async_mirror_write_files b 2
typeset digests="$(async_mirror_digests a 2)
$(async_mirror_digests b 2)"
sync_pool $TESTPOOL
log_mustnot wait_async_mirror_feature enabled 15
log_must zpool export $TESTPOOL
log_must zpool import -d $ASYNC_MIRROR_DIR $TESTPOOL
sync_pool $TESTPOOL
log_mustnot wait_async_mirror_feature enabled 15
log_must zpool export $TESTPOOL

async_mirror_unhide_lagging
async_mirror_import
log_must wait_async_mirror_resilvered
sync_pool $TESTPOOL
log_must test "$(get_async_mirror_lineage lineage_parent)" != 0
log_must test "$(get_async_mirror_feature)" == "active"

async_mirror_disable
log_must test "$(get_async_mirror_lineage lineage_parent)" == 0
log_must wait_async_mirror_feature enabled
log_must test "$digests" == "$(async_mirror_digests a 2)
$(async_mirror_digests b 2)"
log_must verify_pool $TESTPOOL

log_pass "The feature stays active until the copies have met again."
