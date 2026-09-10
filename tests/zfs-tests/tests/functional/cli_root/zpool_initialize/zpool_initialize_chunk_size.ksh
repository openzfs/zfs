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
# Copyright (c) 2026 Matthias Goergens.
#
. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_initialize/zpool_initialize.kshlib

#
# DESCRIPTION:
# Initializing bounds, aligns, and snapshots the initialize chunk size.
#
# STRATEGY:
# 1. Start initializing with zero, sub-512 B, greater-than-16 MiB, and
#    non-512-aligned values. Verify that initializing remains healthy.
# 2. Start with a 512 B chunk, change the tunable while initializing, and
#    suspend the operation.
# 3. Change the tunable while suspended, resume, change it again while active,
#    and verify that initializing remains healthy and can complete.
#

VDEV=$TEST_BASE_DIR/initialize-chunk-size.$$

function cleanup
{
	poolexists $TESTPOOL && destroy_pool $TESTPOOL
	rm -f "$VDEV"
	[[ "$default_chunk_sz" ]] && \
	    log_must set_tunable64 INITIALIZE_CHUNK_SIZE $default_chunk_sz
}
log_onexit cleanup

default_chunk_sz=$(get_tunable INITIALIZE_CHUNK_SIZE)
max_chunk_sz=$((16 * 1024 * 1024))

function wait_for_initialize
{
	typeset retries=50

	while (( retries-- > 0 )); do
		[[ -n "$(initialize_progress $TESTPOOL $VDEV)" ]] && return
		sleep 0.1
	done
	log_fail "Initializing did not start"
}

function initialize_stat # field
{
	typeset field=$1

	zpool status -j --json-flat-vdevs --json-int -i $TESTPOOL | \
	    jq -er --arg field "$field" \
	    '.pools[].vdevs[] | select(has($field)) | .[$field]'
}

function verify_initialize_healthy
{
	typeset state errors

	state=$(initialize_stat init_state) || \
	    log_fail "Could not read initializing state"
	errors=$(initialize_stat init_errors) || \
	    log_fail "Could not read initializing errors"
	[[ "$state" == "ACTIVE" || "$state" == "COMPLETE" ]] || \
	    log_fail "Unexpected initializing state: $state"
	(( errors == 0 )) || log_fail "Initializing reported $errors errors"
}

function test_chunk_size # chunk_size
{
	typeset chunk_size=$1

	log_must set_tunable64 INITIALIZE_CHUNK_SIZE $chunk_size
	log_must zpool create -f $TESTPOOL "$VDEV"
	log_must zpool initialize $TESTPOOL "$VDEV"
	wait_for_initialize
	sleep 1
	verify_initialize_healthy
	log_must zpool destroy -f $TESTPOOL
}

log_must truncate -s $((MINVDEVSIZE * 4)) "$VDEV"

for chunk_size in 0 1 513 $((max_chunk_sz + 1)); do
	test_chunk_size $chunk_size
done

log_must set_tunable64 INITIALIZE_CHUNK_SIZE 512
log_must zpool create -f $TESTPOOL "$VDEV"
log_must zpool initialize $TESTPOOL "$VDEV"
wait_for_initialize
progress=$(initialize_progress $TESTPOOL $VDEV)

# Changing the tunable must not invalidate the active thread's allocation and
# write size.
log_must set_tunable64 INITIALIZE_CHUNK_SIZE $((max_chunk_sz + 1))
sleep 2
verify_initialize_healthy
log_must zpool initialize -s $TESTPOOL "$VDEV"
log_must eval "initialize_prog_line $TESTPOOL $VDEV | grep suspended"
new_progress=$(initialize_progress $TESTPOOL $VDEV)
(( progress <= new_progress )) || log_fail "Initializing progress regressed"

# Resume starts a new thread, which samples and aligns 513 B. A further active
# change must not invalidate that thread's allocation or writes.
log_must set_tunable64 INITIALIZE_CHUNK_SIZE 513
log_must zpool initialize $TESTPOOL "$VDEV"
wait_for_initialize
log_mustnot eval "initialize_prog_line $TESTPOOL $VDEV | grep suspended"
log_must set_tunable64 INITIALIZE_CHUNK_SIZE 0
sleep 2
verify_initialize_healthy
log_must zpool initialize -s $TESTPOOL "$VDEV"
log_must eval "initialize_prog_line $TESTPOOL $VDEV | grep suspended"
progress=$(initialize_progress $TESTPOOL $VDEV)
(( new_progress <= progress )) || log_fail "Initializing progress regressed"

# A final resume samples the upper-bound case and must complete without errors.
log_must set_tunable64 INITIALIZE_CHUNK_SIZE $((max_chunk_sz + 1))
log_must zpool initialize -w $TESTPOOL "$VDEV"
verify_initialize_healthy
[[ "$(initialize_stat init_state)" == "COMPLETE" ]] || \
    log_fail "Initializing did not complete"

log_pass "Initializing bounds, aligns, and snapshots its chunk size"
