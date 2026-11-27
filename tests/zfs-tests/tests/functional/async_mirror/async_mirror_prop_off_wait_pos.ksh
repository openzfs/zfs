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
#	"zpool set write_mode=sync" while the lagging child is behind
#	waits until the child has caught up.  The child then becomes a
#	normal child with no resilver.
#
# STRATEGY:
#	1. Create a pool with a two-way mirror, turn the async mirror on, and write
#	   files a.
#	2. Stop the copy, and write files b, so that the lagging child is
#	   behind.
#	3. Set write_mode=sync.  Verify that the state is "stopping", that
#	   the async mirror stays on while the copy is stopped, and that
#	   "write_mode=behind" is refused meanwhile.
#	4. Restart the copy.  Wait until the state is off.  Verify that no
#	   resilver ran, and that the feature returns to enabled.
#	5. Write files c.  Export the pool, and import the second child
#	   alone.  Verify files a, b and c, and that a scrub finds no
#	   errors.
#

verify_runnable "global"

log_onexit cleanup_async_mirror

async_mirror_create_pool
async_mirror_enable
async_mirror_write_files a 2
log_must wait_async_mirror_idle

async_mirror_suspend_copy 1
async_mirror_write_files b 2
log_must test "$(get_async_mirror_stat pending_txgs)" -gt 0

log_must zpool set write_mode=sync $TESTPOOL $ASYNC_MIRROR_LAGGING
log_must test "$(get_async_mirror_prop)" == sync
log_must test "$(get_async_mirror_prop state)" == stopping
typeset -i i
for (( i = 0; i < 5; i++ )); do
	sync_pool $TESTPOOL
	sleep 1
done
log_must test "$(get_async_mirror_prop state)" == stopping
log_must test "$(get_async_mirror_stat active)" -eq 1
log_mustnot zpool set write_mode=behind $TESTPOOL $ASYNC_MIRROR_LAGGING

async_mirror_suspend_copy 0
log_must wait_async_mirror_state off
log_must test "$(get_async_mirror_stat active)" -eq 0
log_must wait_async_mirror_feature enabled
log_mustnot eval "zpool status $TESTPOOL | grep -q resilver"

async_mirror_write_files c 2
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

log_pass "A switch off waits until the lagging child has caught up."
