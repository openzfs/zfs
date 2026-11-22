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
#	'zpool wait -t async_mirror' waits for existing copy work, and ends
#	when the mode turns off or the copy fails.
#
# STRATEGY:
#	1. Stop the copy, write data, and verify that wait blocks until the
#	   lagging child catches up.
#	2. Stop the copy again, write data, and verify that turning the mode
#	   off ends the wait.
#	3. Start the mode again, fail a copy, and verify that the wait ends.
#

verify_runnable "global"

typeset wait_pid=""
typeset wait_result="$TEST_BASE_DIR/async_mirror_wait.$$.result"
typeset wait_output="$TEST_BASE_DIR/async_mirror_wait.$$.output"
typeset writer_pid=""
typeset writer_stop="$TEST_BASE_DIR/async_mirror_wait.$$.writer.stop"
typeset writer_count="$TEST_BASE_DIR/async_mirror_wait.$$.writer.count"

function cleanup_async_mirror_wait
{
	if [[ -n $writer_pid ]]; then
		touch "$writer_stop"
		wait "$writer_pid" >/dev/null 2>&1
		writer_pid=""
	fi
	if [[ -n $wait_pid ]]; then
		async_mirror_suspend_copy 0 >/dev/null 2>&1
		wait "$wait_pid" >/dev/null 2>&1
		wait_pid=""
	fi
	cleanup_async_mirror
	log_must rm -f "$wait_result" "$wait_output" "$writer_stop" \
	    "$writer_count"
}

function start_async_mirror_wait
{
	rm -f "$wait_result"
	(
		timeout $ASYNC_MIRROR_TIMEOUT zpool wait -t async_mirror \
		    "$TESTPOOL" >"$wait_output" 2>&1
		typeset -i result=$?
		print $result > "$wait_result"
	) &
	wait_pid=$!
}

function check_async_mirror_waiting
{
	# One second shows that the suspended copy keeps zpool wait blocked.
	sleep 1
	[[ ! -e $wait_result ]] ||
	    log_fail "zpool wait returned while the async mirror was behind"
}

function finish_async_mirror_wait # success|cancelled
{
	log_must wait "$wait_pid"
	wait_pid=""
	if [[ $1 == success ]]; then
		log_must test "$(cat "$wait_result")" -eq 0
	else
		log_must test "$(cat "$wait_result")" -ne 0
		log_must grep -q "async mirror did not catch up in pool" \
		    "$wait_output"
		log_must grep -q "turned off, failed or froze" "$wait_output"
		log_mustnot grep -q "delegated" "$wait_output"
		log_must test "$(cat "$wait_result")" -eq 1
	fi
	log_must rm -f "$wait_result" "$wait_output"
}

function start_async_mirror_wait_writer
{
	rm -f "$writer_stop" "$writer_count"
	(
		typeset -i i=0
		while [[ ! -e $writer_stop ]]; do
			#
			# Four files, one block each.  The pool can still fill
			# up: while the copy is behind, the block that a write
			# replaces and the freed metadata of each txg are held.
			# A writer with no pause made some hundred txgs in a
			# second, and the held space filled the pool before
			# the copy started.  So write twice in a second.
			#
			file_write -o create -f \
			    "/$TESTPOOL/wait_writer.$((i % 4))" \
			    -b 131072 -c 1 -d R >/dev/null 2>&1 || exit 1
			zpool sync -l "$TESTPOOL" >/dev/null 2>&1 || exit 1
			((i++))
			print $i > "$writer_count"
			sleep 0.5
		done
	) &
	writer_pid=$!
}

function wait_async_mirror_writer_count # count
{
	typeset -i i
	for ((i = 0; i < ASYNC_MIRROR_TIMEOUT; i++)); do
		if [[ -s $writer_count ]] &&
		    (( $(cat "$writer_count") >= $1 )); then
			return 0
		fi
		sleep 1
	done
	log_fail "writer did not complete $1 writes while suspended"
}

log_onexit cleanup_async_mirror_wait

async_mirror_create_pool
async_mirror_enable

async_mirror_suspend_copy 1
async_mirror_write_files plain 2
log_must test "$(get_async_mirror_stat pending_txgs)" -gt 0
log_must timeout 10 zpool wait $TESTPOOL
async_mirror_suspend_copy 0
log_must wait_async_mirror_idle

async_mirror_suspend_copy 1
async_mirror_write_files a 2
log_must test "$(get_async_mirror_stat pending_txgs)" -gt 0
typeset target_txg=$(get_async_mirror_stat synced_txg)
start_async_mirror_wait
check_async_mirror_waiting
start_async_mirror_wait_writer
wait_async_mirror_writer_count 4
check_async_mirror_waiting
#
# Slow every I/O to the lagging child, so that the copy is always behind the
# writer and a record is always pending while the writer runs.  A wait that
# takes a new target on each pass (the old loop) never ends, and the timeout
# of the wait command makes this test fail instead of hang.
#
typeset lag_guid=$(get_leaf_guid $ASYNC_MIRROR_LAGGING)
log_must zinject -d $lag_guid -D 200:1 $TESTPOOL
async_mirror_suspend_copy 0
finish_async_mirror_wait success
log_must kill -0 "$writer_pid"
log_must test "$(cat "$writer_count")" -ge 4
log_must test "$(get_async_mirror_stat pending_txgs)" -gt 0
log_must touch "$writer_stop"
log_must wait "$writer_pid"
writer_pid=""
log_must test "$(get_async_mirror_stat applied_txg)" -ge "$target_txg"
log_must zinject -c all
log_must wait_async_mirror_idle

async_mirror_suspend_copy 1
async_mirror_write_files b 2
log_must test "$(get_async_mirror_stat pending_txgs)" -gt 0
start_async_mirror_wait
check_async_mirror_waiting
log_must zpool set write_mode=sync $TESTPOOL $ASYNC_MIRROR_LAGGING
finish_async_mirror_wait cancelled
log_must test "$(get_async_mirror_prop state)" == stopping
async_mirror_suspend_copy 0
log_must wait_async_mirror_state off
log_must wait_async_mirror_feature enabled

async_mirror_enable
async_mirror_suspend_copy 1
async_mirror_write_files c 2
log_must test "$(get_async_mirror_stat pending_txgs)" -gt 0
typeset guid=$(get_leaf_guid $ASYNC_MIRROR_LAGGING)
log_must zinject -d $guid -e io -T write $TESTPOOL
start_async_mirror_wait
check_async_mirror_waiting
async_mirror_suspend_copy 0
finish_async_mirror_wait cancelled
log_must test "$(get_async_mirror_stat failed)" -eq 1
typeset status_output=$(zpool status $TESTPOOL 2>&1)
echo "$status_output" | grep -Fq "lagging child (failed)" ||
	log_fail "zpool status did not show the failed mirror state" \
	    "$status_output"
status_output=$(zpool status -x $TESTPOOL 2>&1)
echo "$status_output" | grep -Fq "asynchronous mirror copy has failed" ||
	log_fail "zpool status -x did not report the failed mirror" \
	    "$status_output"
log_must zinject -c all

log_pass "zpool wait handles async mirror progress and termination."
