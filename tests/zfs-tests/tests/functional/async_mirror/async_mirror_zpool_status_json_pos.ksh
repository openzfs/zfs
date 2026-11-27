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
#	'zpool status -j' reports async mirror progress and the lagging leaf.
#
# STRATEGY:
#	1. Create a two-way mirror and turn on the async mirror.
#	2. Stop the copy and write data to make the lagging child fall behind.
#	3. Check the JSON pool statistics and the lagging leaf properties:
#	   the values of the pool are in the pool object, and the values of
#	   the lagging child are in the object of its leaf.
#

verify_runnable "global"

function cleanup_async_mirror_status_json
{
	cleanup_async_mirror
	log_must rm -f "$TEST_BASE_DIR/async_mirror_status.json"
}

log_onexit cleanup_async_mirror_status_json

async_mirror_create_pool
async_mirror_suspend_copy 1
async_mirror_enable
async_mirror_write_files a 2
log_must test "$(get_async_mirror_stat pending_txgs)" -gt 0

typeset status_file=$TEST_BASE_DIR/async_mirror_status.json
log_must eval "zpool status -j -L --json-int $TESTPOOL > $status_file"
log_must jq -e --arg pool "$TESTPOOL" '
	.pools[$pool] as $pool_status |
	($pool_status.async_mirror.synced_txg | type) == "number" and
	($pool_status.async_mirror | has("applied_txg") | not) and
	($pool_status.async_mirror | has("pending_bytes") | not) and
	($pool_status.async_mirror | has("state") | not) and
	([ $pool_status.vdevs | .. | objects |
	    select(has("read_mode") and has("write_mode")) ] as $modes |
	    ($modes | length) == 2 and
	    ([ $modes[] | select(.read_mode == "normal" and
		.write_mode == "sync") ] | length) == 1 and
	    ([ $modes[] | select(.read_mode == "fallback" and
		.write_mode == "behind") ] | length) == 1) and
	([ $pool_status.vdevs | .. | objects |
	    select(.async_mirror_lagging == 1) ] as $lagging |
	    ($lagging | length) == 1 and
	    ($lagging[0].async_mirror_applied_txg | type) == "number" and
	    ($lagging[0].async_mirror_pending_bytes | type) == "number" and
	    $lagging[0].async_mirror_applied_txg <=
	    $pool_status.async_mirror.synced_txg and
	    $lagging[0].async_mirror_state == "behind")
' "$status_file"

log_pass "zpool status JSON reports async mirror progress."
