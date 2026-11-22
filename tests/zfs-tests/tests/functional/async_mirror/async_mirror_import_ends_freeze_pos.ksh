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
#	An import that ends a freeze removes the freeze entry in its first
#	txg, including when no release runs before export.  This prevents a
#	later import from treating the child as frozen and resilvering it in
#	full.
#
# STRATEGY:
#	1. Create a pool with a two-way mirror and turn the async mirror on.  Write
#	   files a and wait until the lagging child has caught up.
#	2. Damage every copy of the indirect block of a new file b on the
#	   leader before its copy, and wait until the lagging child freezes.
#	   Export, and verify that the MOS has the freeze entry.
#	3. Import, and wait until the records are released.  Stop the
#	   release, which records the end of the freeze, and set the
#	   property of the lagging child to off.  Wait until the copy is
#	   failed, export, and verify that the MOS still has the freeze
#	   entry, as after a crash.
#	4. Import with the release still stopped: the freeze ends.  Export.
#	5. Verify that the MOS has no freeze entry.
#

verify_runnable "global"

log_onexit cleanup_async_mirror

#
# Succeed if the MOS of the exported test pool has the freeze entry: the
# frozen flag in the per-child entry of the lagging child.
#
function has_freeze_entry
{
	async_mirror_child_has_flag frozen -e -p $ASYNC_MIRROR_DIR
}

async_mirror_create_pool
async_mirror_enable
async_mirror_write_files a 2
log_must wait_async_mirror_idle
async_mirror_damage_backlog /$TESTPOOL/b 1 0
log_must wait_async_mirror_frozen
sync_pool $TESTPOOL
log_must zpool export $TESTPOOL
log_must has_freeze_entry

async_mirror_import
log_must wait_async_mirror_released
async_mirror_suspend_release 1
log_must zpool set write_mode=sync $TESTPOOL $ASYNC_MIRROR_LAGGING
log_must wait_async_mirror_state failed
log_must zpool export $TESTPOOL
log_must has_freeze_entry

async_mirror_import
log_must test "$(get_async_mirror_stat frozen)" == 0
log_must zpool export $TESTPOOL
log_mustnot has_freeze_entry

log_pass "The import that ends a freeze removes the freeze entry."
