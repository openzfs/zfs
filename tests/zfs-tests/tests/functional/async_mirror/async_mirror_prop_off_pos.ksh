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
#	"zpool set write_mode=sync" makes the lagging child a normal child
#	of its mirror again, with no export and no resilver.  Its records
#	are released, and the feature returns to enabled.
#
# STRATEGY:
#	1. Create a pool with a two-way mirror, turn the async mirror on, write
#	   files a, and wait until the lagging child has caught up.
#	2. Set write_mode=sync on the lagging child.  Wait until its state
#	   is off.  Verify the property and the kstat, with no export.
#	3. Verify that the feature returns to enabled, that the mark is
#	   gone, and that no resilver ran.
#	4. Write files b.  They reach both children.
#	5. Export the pool, and import the second child alone.  Verify
#	   files a and b, and that a scrub finds no errors.
#

verify_runnable "global"

log_onexit cleanup_async_mirror

function has_mark # vdev file
{
	zdb -l $1 | grep -q async_mirror_lagging
}

async_mirror_create_pool
async_mirror_enable
async_mirror_write_files a 2
log_must wait_async_mirror_idle
log_must test "$(get_async_mirror_feature)" == active

log_must zpool set write_mode=sync $TESTPOOL $ASYNC_MIRROR_LAGGING
log_must test "$(get_async_mirror_prop)" == sync
log_must wait_async_mirror_state off
log_must test "$(get_async_mirror_prop state $ASYNC_MIRROR_LEADER)" == off
log_must test "$(get_async_mirror_stat active)" -eq 0

log_must wait_async_mirror_feature enabled
sync_pool $TESTPOOL
log_mustnot has_mark $ASYNC_MIRROR_LEADER
log_mustnot has_mark $ASYNC_MIRROR_LAGGING
log_mustnot eval "zpool status $TESTPOOL | grep -q resilver"

async_mirror_write_files b 2
typeset digests="$(async_mirror_digests a 2)
$(async_mirror_digests b 2)"
log_mustnot eval "zpool status $TESTPOOL | grep -q resilver"

log_must zpool export $TESTPOOL
async_mirror_import_lagging
log_must test "$digests" == "$(async_mirror_digests a 2)
$(async_mirror_digests b 2)"
log_must verify_pool $TESTPOOL
log_must check_pool_status $TESTPOOL errors "No known data errors"

log_pass "The property turns the async mirror off with no resilver."
