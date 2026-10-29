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
# Copyright (c) 2026, Patrick Domack. All rights reserved.
#

. $STF_SUITE/tests/functional/clonedup/clonedup.kshlib

#
# DESCRIPTION:
#	A run restarted by an import keeps the txg bound it started
#	with.  The partitions finished before the export were walked
#	up to that bound, so the blocks born after it belong to the
#	next run.
#
# STRATEGY:
#	1. Cap the index so the run takes several partitions, and slow
#	   the apply so the run outlasts the steps below.
#	2. Start a run and wait for its second partition.
#	3. Write a file and a copy of it, then export and import.
#	4. Let the run finish.  Its bound and its last txg must be the
#	   bound it started with, not the import txg.
#	5. The next run must share every block of the copy.
#

verify_runnable "global"

typeset -i nblk=32
typeset -i delay_ms=20
typeset -i bound=180

function cleanup
{
	log_must restore_tunable CLONEDUP_SCAN_MEM_MAX
	log_must restore_tunable CLONEDUP_APPLY_TXG_DELAY
	log_must restore_tunable CLONEDUP_APPLY_THREADS
	clonedup_cleanup
}

# Wait until the run has left its first partition.
function wait_second_partition # [timeout]
{
	typeset timeout=${1:-120}
	typeset i part

	for ((i = 0; i < timeout * 5; i++)); do
		part=$(clonedup_jstat clonedup_partition)
		[[ -n $part ]] && (( part > 0 )) && return 0
		clonedup_is_running ||
		    log_fail "the run ended before partition two"
		sleep 0.2
	done
	log_fail "no second partition within $timeout seconds"
}

log_assert "a run restarted by import keeps its txg bound"
log_onexit cleanup

log_must save_tunable CLONEDUP_SCAN_MEM_MAX
log_must save_tunable CLONEDUP_APPLY_TXG_DELAY
log_must save_tunable CLONEDUP_APPLY_THREADS

clonedup_pool_create -O recordsize=8k
log_must dd if=/dev/urandom of=/$TESTPOOL/big bs=8k count=1024 \
    status=none
log_must dd if=/$TESTPOOL/big of=/$TESTPOOL/big2 bs=8k status=none
clonedup_sync

log_must set_tunable64 CLONEDUP_SCAN_MEM_MAX $((32 * 1024))
log_must set_tunable32 CLONEDUP_APPLY_TXG_DELAY $delay_ms
log_must set_tunable32 CLONEDUP_APPLY_THREADS 1

log_must zpool clonedup $TESTPOOL
clonedup_wait_running
typeset max_txg=$(clonedup_jstat clonedup_max_txg)
clonedup_assert_counts "$max_txg"
log_note "the run's bound is txg $max_txg"
wait_second_partition

clonedup_write /$TESTPOOL/c $nblk 8192
clonedup_dup /$TESTPOOL/c /$TESTPOOL/d
clonedup_sync
clonedup_is_running || log_fail "the run ended before the export"

clonedup_bounded $bound zpool export $TESTPOOL
clonedup_bounded $bound zpool import $TESTPOOL
clonedup_bounded $bound zpool wait -t clonedup $TESTPOOL
clonedup_jstat_is state FINISHED
clonedup_jstat_is errors 0
clonedup_record_is max_txg $max_txg
clonedup_record_is last_txg $max_txg
clonedup_check_shared $TESTPOOL big $TESTPOOL big2 \
    "$(clonedup_all_blocks 1024)"

# The blocks born after the bound are the next run's.
clonedup_run
clonedup_check_shared $TESTPOOL c $TESTPOOL d \
    "$(clonedup_all_blocks $nblk)"
log_must cmp /$TESTPOOL/c /$TESTPOOL/d
clonedup_leakcheck

log_pass "a run restarted by import keeps its txg bound"
