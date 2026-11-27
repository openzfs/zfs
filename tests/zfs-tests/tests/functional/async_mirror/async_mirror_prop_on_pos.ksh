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
#	"zpool set write_mode=behind" turns the async mirror on at once, with no
#	export and no import.  The lagging child then gets each txg from
#	the replay.
#
# STRATEGY:
#	1. Create a pool with a two-way mirror and write files a.  Verify
#	   that the property and the state of the second child are off.
#	2. Set write_mode=behind on the second child.  Verify the
#	   property, the state and the kstat, with no export.
#	3. Verify that the state of the first child is "leader", and that
#	   the mark is in the labels of both children.  The switch on
#	   rewrites the labels of the lagging child at the txg where the
#	   async mirror starts, so their config txg is not
#	   above the newest uberblock of the child.
#	4. Write files b, and wait until the lagging child has caught up.
#	5. Export the pool, and import the lagging child alone.  Verify
#	   files a and b, and that a scrub finds no errors.
#

verify_runnable "global"

log_onexit cleanup_async_mirror

function has_mark # vdev file
{
	zdb -l $1 | grep -q async_mirror_lagging
}

async_mirror_create_pool
async_mirror_write_files a 2
log_must test "$(get_async_mirror_prop)" == sync
log_must test "$(get_async_mirror_prop state)" == off
log_must test "$(get_async_mirror_stat active)" -eq 0

log_must zpool set write_mode=behind $TESTPOOL $ASYNC_MIRROR_LAGGING
log_must test "$(get_async_mirror_prop)" == behind
log_must test "$(get_async_mirror_stat active)" -eq 1
log_must wait_async_mirror_state "behind|current"
log_must test "$(get_async_mirror_prop state $ASYNC_MIRROR_LEADER)" == leader
log_must test \
    "$(get_async_mirror_prop write_mode $ASYNC_MIRROR_LEADER)" == sync
sync_pool $TESTPOOL
log_must has_mark $ASYNC_MIRROR_LEADER
log_must has_mark $ASYNC_MIRROR_LAGGING
log_must label_txg_held $ASYNC_MIRROR_LAGGING

async_mirror_write_files b 2
typeset digests="$(async_mirror_digests a 2)
$(async_mirror_digests b 2)"
log_must wait_async_mirror_idle
log_must wait_async_mirror_state current

log_must zpool export $TESTPOOL
async_mirror_import_lagging
log_must test "$digests" == "$(async_mirror_digests a 2)
$(async_mirror_digests b 2)"
log_must verify_pool $TESTPOOL
log_must check_pool_status $TESTPOOL errors "No known data errors"

log_pass "The property turns the async mirror on with no export."
