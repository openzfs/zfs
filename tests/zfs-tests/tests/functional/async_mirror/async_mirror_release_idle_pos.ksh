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
#	Held space is released on an idle pool, even if the release of
#	the backlog needs more than one sync task.
#
# STRATEGY:
#	1. Set a low limit on the entries of one release sync task.
#	2. Create a pool with a two-way mirror and turn the async mirror on.
#	3. Create many small files, and remove every second file.
#	4. Wait until the lagging child has caught up.
#	5. Verify that the held space falls below 2 MiB with no new writes.
#

verify_runnable "global"

typeset -r limit=$(get_tunable ASYNC_MIRROR_RELEASE_LIMIT)

function cleanup
{
	log_must async_mirror_set_tunable64 ASYNC_MIRROR_RELEASE_LIMIT $limit
	cleanup_async_mirror
}

log_onexit cleanup

log_must async_mirror_set_tunable64 ASYNC_MIRROR_RELEASE_LIMIT 256
async_mirror_create_pool
async_mirror_enable

typeset -i i

for (( i = 0; i < 1024; i++ )); do
	file_write -o create -f /$TESTPOOL/f.$i -b 8192 -c 1 -d R || \
	    log_fail "Cannot write f.$i"
done
sync_pool $TESTPOOL
log_must wait_async_mirror_idle

for (( i = 0; i < 1024; i += 2 )); do
	rm /$TESTPOOL/f.$i || log_fail "Cannot remove f.$i"
done
sync_pool $TESTPOOL
log_must wait_async_mirror_idle

log_must wait_async_mirror_stat held_bytes -lt 2097152

log_pass "Held space is released on an idle pool."
