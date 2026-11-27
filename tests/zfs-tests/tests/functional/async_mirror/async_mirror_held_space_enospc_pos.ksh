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
#	Held frees do not cause writes to hang at ENOSPC, and released space
#	becomes available after the lagging child catches up.
#
# STRATEGY:
#	1. Create an async mirror and suspend its copy and free release.
#	   Turn the hard limit of the held space off, so that the async
#	   mirror does not give up on the lagging child.
#	2. Fill the pool, delete files, and verify that their space stays held.
#	3. Verify that a bounded write fails with ENOSPC, not a timeout.
#	4. Resume copying and release, then verify that space and writes return.
#

verify_runnable "global"

log_onexit cleanup_async_mirror
log_assert "Held frees do not hide ENOSPC or block writes."

ASYNC_MIRROR_VDEV_SIZE=256m
async_mirror_create_pool
async_mirror_enable
log_must async_mirror_set_tunable32 ASYNC_MIRROR_HELD_LIMIT_PERCENT 0
log_must async_mirror_set_tunable32 ASYNC_MIRROR_HELD_FREE_PERCENT 0
async_mirror_suspend_copy 1
async_mirror_suspend_release 1

typeset -i i
typeset -i fill_count=0
typeset -i status=0
typeset output=$TEST_BASE_DIR/async_mirror_enospc.out

for (( i = 0; i < 128; i++ )); do
	if timeout 60 file_write -o create -f /$TESTPOOL/fill.$i \
	    -b 131072 -c 16 -d R >$output 2>&1; then
		(( fill_count++ ))
		sync_pool $TESTPOOL
	else
		status=$?
		break
	fi
done

log_must test $fill_count -gt 0
log_must test $status -ne 0
log_must test $status -ne 124
log_must grep -qi "No space left on device" $output

typeset available_full=$(zfs get -Hp -o value available $TESTPOOL)
log_must rm -f /$TESTPOOL/fill.*
sync_pool $TESTPOOL
typeset -i held=$(get_async_mirror_stat held_bytes)
typeset available_held=$(zfs get -Hp -o value available $TESTPOOL)
log_must test $held -gt 0
log_must test $available_held -le $available_full

if timeout 60 file_write -o create -f /$TESTPOOL/blocked \
    -b 131072 -c 16 -d R >$output 2>&1; then
	log_fail "A write succeeded while the deleted space was held"
else
	status=$?
fi
log_must test $status -ne 124
log_must grep -qi "No space left on device" $output

async_mirror_suspend_copy 0
async_mirror_suspend_release 0
log_must wait_async_mirror_idle 600
#
# The async mirror stays on, so its record store stays, and each new txg
# holds a little metadata.  Wait until most of the held space is released.
# A release joins a txg that syncs, so sync the pool.
#
for (( i = 0; i < 600; i++ )); do
	(( $(get_async_mirror_stat held_bytes) < held / 4 )) && break
	sync_pool $TESTPOOL
	sleep 1
done
log_must test "$(get_async_mirror_stat held_bytes)" -lt $(( held / 4 ))

typeset available_released=$(zfs get -Hp -o value available $TESTPOOL)
log_must test $available_released -gt $available_held
log_must timeout 60 file_write -o create -f /$TESTPOOL/recovered \
    -b 131072 -c 16 -d R

log_pass "Held frees return after the async mirror copy resumes."
