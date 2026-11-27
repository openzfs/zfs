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
#	Plain 'zpool sync' waits for a healthy async mirror copy, and returns
#	without waiting when the copy is frozen, failed, switched off, or
#	suspended.  '-l' keeps the leader-only behavior.  A signal interrupts
#	the wait.
#
# STRATEGY:
#	1. Slow the copy and verify plain sync waits until the lagging child
#	   applies the synced txg.
#	2. Suspend the copy and verify '-l' returns with pending txgs.
#	3. Send SIGINT to a sync waiting for a slow copy and verify it exits.
#	4. Freeze and fail the lagging child, then verify sync reports the
#	   early exit and returns success.
#	5. Start a normal resilver after a freeze, and verify that a later
#	   async copy makes plain sync wait again.
#

verify_runnable "global"

typeset sync_result="$TEST_BASE_DIR/async_mirror_sync.$$.result"
typeset sync_stdout="$TEST_BASE_DIR/async_mirror_sync.$$.stdout"
typeset sync_stderr="$TEST_BASE_DIR/async_mirror_sync.$$.stderr"
typeset sync_pid=""
typeset -i signal_result

function cleanup_async_mirror_sync
{
	if [[ -n $sync_pid ]]; then
		zinject -c all >/dev/null 2>&1
		set_tunable32 ASYNC_MIRROR_SUSPEND_COPY 0 >/dev/null 2>&1
		set_tunable32 ASYNC_MIRROR_COPY_DELAY_MS 0 >/dev/null 2>&1
		wait "$sync_pid" >/dev/null 2>&1
		sync_pid=""
	fi
	cleanup_async_mirror
}

function start_async_mirror_sync
{
	: > "$sync_result"
	(
		timeout "$ASYNC_MIRROR_TIMEOUT" zpool sync "$TESTPOOL" \
		    >"$sync_stdout" 2>"$sync_stderr"
		print $? > "$sync_result"
	) &
	sync_pid=$!
}

function finish_async_mirror_sync
{
	log_must wait "$sync_pid"
	sync_pid=""
	log_must test "$(cat "$sync_result")" -eq 0
}

function check_sync_returns_early # state
{
	typeset state=$1
	typeset -i result

	if timeout 10 zpool sync "$TESTPOOL" >"$sync_stdout" 2>"$sync_stderr"
	then
		result=0
	else
		result=$?
	fi
	log_must test "$result" -eq 0
	log_must test ! -s "$sync_stdout"
	log_must grep -q "async mirror did not catch up in pool '$TESTPOOL'" \
	    "$sync_stderr"
	log_note "zpool sync returned early for a $state async mirror"
}

log_onexit cleanup_async_mirror_sync

async_mirror_create_pool
async_mirror_enable

# Keep enough data pending for the copy delay to make the wait observable.
log_must async_mirror_set_tunable32 ASYNC_MIRROR_COPY_DELAY_MS 50
async_mirror_suspend_copy 1
async_mirror_write_files slow 4 32
log_must test "$(get_async_mirror_stat pending_txgs)" -gt 0
# The sync waits until the lagging child has the data written before it.
# The child txg can stay below the synced txg: txgs that only release
# records have nothing to copy.
typeset -i target=$(async_mirror_file_birth slow.3)
async_mirror_suspend_copy 0
start_async_mirror_sync
sleep 1
log_must test ! -s "$sync_result"
finish_async_mirror_sync
log_must test "$(get_async_mirror_stat applied_txg)" -ge $target

# A leader-only sync must return while the lagging child remains behind.
async_mirror_suspend_copy 1
async_mirror_write_files leader 2 16
log_must test "$(get_async_mirror_stat pending_txgs)" -gt 0
check_sync_returns_early suspended
log_must timeout 10 zpool sync -l "$TESTPOOL"
log_must test "$(get_async_mirror_stat applied_txg)" -lt \
    "$(get_async_mirror_stat synced_txg)"
async_mirror_suspend_copy 0
log_must wait_async_mirror_idle

# SIGINT interrupts the caller's wait while the copy is still slow.
log_must async_mirror_set_tunable32 ASYNC_MIRROR_COPY_DELAY_MS 1000
async_mirror_suspend_copy 1
async_mirror_write_files signal 4 32
log_must test "$(get_async_mirror_stat pending_txgs)" -gt 0
async_mirror_suspend_copy 0
if timeout -s INT -k 5 1 zpool sync "$TESTPOOL" \
    >"$sync_stdout" 2>"$sync_stderr"; then
	signal_result=0
else
	signal_result=$?
fi
log_must test "$signal_result" -eq 124
log_must test "$(get_async_mirror_stat pending_txgs)" -gt 0
log_must async_mirror_set_tunable32 ASYNC_MIRROR_COPY_DELAY_MS 0
log_must wait_async_mirror_idle

# A frozen child cannot apply the target txg.  Sync itself still succeeds.
async_mirror_freeze_by_stall /$TESTPOOL/freeze
log_must test "$(get_async_mirror_stat frozen)" -eq 1
check_sync_returns_early frozen

# A frozen child does not delay the sync command. Only the administrator
# starts a normal resilver to make it a normal child again.
log_must async_mirror_set_tunable32 ASYNC_MIRROR_STALL_SECS 30
# End the injected stall first: each I/O of the child takes 6 seconds, and
# the resilver must write the child.
log_must zinject -c all
log_must zpool set write_mode=sync $TESTPOOL $ASYNC_MIRROR_LAGGING
log_must wait_async_mirror_resilvered
log_must test "$(get_async_mirror_prop)" == sync
log_must zpool set write_mode=behind $TESTPOOL $ASYNC_MIRROR_LAGGING
log_must wait_async_mirror_state "behind|current"
log_must wait_async_mirror_idle

# A failed child also ends the wait without changing the sync result.
async_mirror_suspend_copy 1
async_mirror_write_files failed 1
log_must zinject -d $(get_leaf_guid $ASYNC_MIRROR_LAGGING) \
    -e io -T write $TESTPOOL
async_mirror_suspend_copy 0
log_must wait_async_mirror_failed
check_sync_returns_early failed

log_pass "zpool sync waits for healthy async mirror copies safely."
