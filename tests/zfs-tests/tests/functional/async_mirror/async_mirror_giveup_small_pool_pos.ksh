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

# shellcheck disable=SC1091
. "$STF_SUITE"/tests/functional/async_mirror/async_mirror.kshlib

#
# DESCRIPTION:
#	A small pool gives up on a lagging child before held frees exhaust
#	allocator space.
#
# STRATEGY:
#	1. Run repeated writes and snapshot destroys on a plain file-vdev
#	   mirror.  This workload succeeds without an async mirror.
#	2. Create the same small pool with an async mirror.  Stop the copy,
#	   write one newer txg, and import from the lagging child alone.
#	   Verify that the child serves its applied txg.
#	3. Repeat the write and snapshot-destroy workload.  Verify that the
#	   async mirror gives up before an allocation fails, releases the
#	   held space, and leaves the pool online.
#

verify_runnable "global"

log_onexit cleanup_async_mirror
log_assert "A small pool gives up before held frees cause ENOSPC."

# shellcheck disable=SC2034
ASYNC_MIRROR_VDEV_SIZE=128m
typeset -i i cycles=24 mib=$(( 1024 * 1024 )) held

function async_mirror_snapshot_cycle # file
{
	typeset file=$1

	async_mirror_write "/$TESTPOOL/$file" 32
	sync_pool "$TESTPOOL"
	log_must zfs snapshot "$TESTPOOL@$file"
	log_must rm "/$TESTPOOL/$file"
	log_must zfs destroy "$TESTPOOL@$file"
	sync_pool "$TESTPOOL"
}

function wait_async_mirror_space_release
{
	typeset -i wait_i

	for (( wait_i = 0; wait_i < ASYNC_MIRROR_TIMEOUT; wait_i++ )); do
		held=$(get_async_mirror_stat held_bytes)
		(( held < mib )) && return 0
		sync_pool "$TESTPOOL"
		sleep 1
	done
	log_note "held space stayed at $held bytes after the give-up"
	return 1
}

# Establish that the same workload succeeds without the async mirror.
async_mirror_create_pool
log_must zfs set compression=off "$TESTPOOL"
for (( i = 0; i < cycles; i++ )); do
	async_mirror_snapshot_cycle plain.$i
done
log_must zpool destroy -f "$TESTPOOL"

# Import the lagging child's own txg before the space give-up starts.
async_mirror_create_pool
log_must zfs set compression=off "$TESTPOOL"
async_mirror_enable
async_mirror_write_files seed 1 8
log_must wait_async_mirror_idle
typeset seed_digest
seed_digest=$(async_mirror_digests seed 1)
async_mirror_suspend_copy 1
async_mirror_write_files behind 1 1
log_must test "$(get_async_mirror_stat pending_txgs)" -gt 0
log_must zpool export "$TESTPOOL"
log_must mv "$ASYNC_MIRROR_LEADER" "$ASYNC_MIRROR_HIDDEN"
log_must zpool import -o readonly=on -d "$ASYNC_MIRROR_DIR" "$TESTPOOL"
log_must test "$seed_digest" == "$(async_mirror_digests seed 1)"
log_mustnot test -e "/$TESTPOOL/behind.0"
log_must zpool export "$TESTPOOL"
log_must mv "$ASYNC_MIRROR_HIDDEN" "$ASYNC_MIRROR_LEADER"
async_mirror_import
log_must test "$(get_async_mirror_stat giveups)" -eq 0

log_must async_mirror_set_tunable32 ASYNC_MIRROR_HELD_LIMIT_PERCENT 0
log_must async_mirror_set_tunable32 ASYNC_MIRROR_HELD_FREE_PERCENT 2
log_must async_mirror_set_tunable32 ASYNC_MIRROR_STALL_SECS 0

for (( i = 0; i < cycles; i++ )); do
	async_mirror_snapshot_cycle async.$i
	[[ $(get_async_mirror_stat frozen) == 1 ]] && break
done

log_must test "$(get_async_mirror_stat frozen)" -eq 1
log_must test "$(get_async_mirror_stat giveups)" -eq 1
log_must test "$(get_async_mirror_stat giveup_reason)" -eq 2
log_must test "$(get_async_mirror_prop state)" == frozen
log_must wait_async_mirror_space_release

# The plain pool completed all cycles.  Finish the same count after release.
for (( i = i + 1; i < cycles; i++ )); do
	async_mirror_snapshot_cycle async.$i
done

log_must test "$(get_async_mirror_stat frozen)" -eq 1
log_must test "$(get_async_mirror_stat held_bytes)" -lt $mib
log_must check_pool_status "$TESTPOOL" state ONLINE

log_pass "The async mirror gives up before held frees exhaust pool space."
