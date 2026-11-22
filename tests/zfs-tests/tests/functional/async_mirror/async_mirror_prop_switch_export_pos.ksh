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
#	An export in the middle of a switch acts as a crash at the switch:
#	the lagging child is behind, and the records stay.  After a switch
#	on, the import resumes the copy.  After a switch off that did not
#	end, the property is off, and the import releases the records and
#	resilvers the txgs that the child had not got.  No data is lost.
#
# STRATEGY:
#	1. Create a pool with a two-way mirror and write files a.  Stop the
#	   copy, and turn the async mirror on.
#	2. Write files b, and export the pool: the lagging child holds only
#	   the txg before the switch.
#	3. Restart the copy and import.  Verify that the async mirror is on again,
#	   and wait until the lagging child has caught up.
#	4. Stop the copy, write files c, set the property off, and export
#	   while the state is "stopping".
#	5. Restart the copy and import.  Verify that the property and the
#	   async mirror states are off, and wait for the resilver.
#	6. Export the pool, and import the second child alone.  Verify files
#	   a, b and c, and that a scrub finds no errors.
#

verify_runnable "global"

log_onexit cleanup_async_mirror

async_mirror_create_pool
async_mirror_write_files a 2
async_mirror_suspend_copy 1
async_mirror_enable
async_mirror_export_behind b 2

async_mirror_suspend_copy 0
async_mirror_import
log_must test "$(get_async_mirror_stat active)" -eq 1
log_must wait_async_mirror_idle
log_must wait_async_mirror_state current

async_mirror_suspend_copy 1
async_mirror_write_files c 2
log_must test "$(get_async_mirror_stat pending_txgs)" -gt 0
log_must zpool set write_mode=sync $TESTPOOL $ASYNC_MIRROR_LAGGING
log_must test "$(get_async_mirror_prop state)" == stopping
log_must zpool export $TESTPOOL

async_mirror_suspend_copy 0
async_mirror_import
log_must test "$(get_async_mirror_prop)" == sync
log_must test "$(get_async_mirror_prop state)" == off
log_must test "$(get_async_mirror_stat active)" -eq 0
log_must wait_async_mirror_resilvered
log_must wait_async_mirror_feature enabled
typeset digests="$(async_mirror_digests a 2)
$(async_mirror_digests b 2)
$(async_mirror_digests c 2)"

log_must zpool export $TESTPOOL
async_mirror_import_lagging
log_must test "$digests" == "$(async_mirror_digests a 2)
$(async_mirror_digests b 2)
$(async_mirror_digests c 2)"
log_must verify_pool $TESTPOOL
log_must check_pool_status $TESTPOOL errors "No known data errors"

log_pass "An export at a switch of the property loses no data."
