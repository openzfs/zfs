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
#	A run's memory stays within zfs_clonedup_scan_mem_max.  The
#	order pass gives each copy it moves into the apply tree a
#	source of its own, so the index splits early enough to leave
#	room for them.  The counting filter takes at most half the
#	cap and the index the rest.  The mem_peak kstat records the
#	most the index and the filter held at once.
#
# STRATEGY:
#	1. Write a file of 8K blocks and four byte copies of it, so
#	   each block's group has five copies and the order pass
#	   moves four of them.
#	2. Cap the run at 64 KiB, far below what one partition would
#	   need.
#	3. Run clonedup without the counting filter, then on a new
#	   pool with the filter forced on.
#	4. After each run, check that it split into partitions,
#	   cloned every copy, and held no more than the cap at once.
#

verify_runnable "global"

typeset -ri nblk=512
typeset -ri cap=65536
typeset all="$(clonedup_all_blocks $nblk)"

function cleanup
{
	log_must restore_tunable CLONEDUP_INDEX_FILTER
	log_must restore_tunable CLONEDUP_SCAN_MEM_MAX
	clonedup_cleanup
}

# Five copies of nblk distinct 8K blocks.
function build_pool
{
	typeset -i i

	clonedup_pool_create -O recordsize=8k
	log_must dd if=/dev/urandom of=/$TESTPOOL/a bs=8k \
	    count=$nblk status=none
	for ((i = 1; i <= 4; i++)); do
		log_must dd if=/$TESTPOOL/a of=/$TESTPOOL/c$i bs=8k \
		    status=none
	done
	clonedup_sync
}

# The run split, cloned every copy and stayed within the cap.
function check_run
{
	typeset peak=$(clonedup_kstat mem_peak)
	typeset -i i

	log_note "mem_peak '$peak', cap $cap"
	[[ $peak == +([0-9]) ]] ||
	    log_fail "no mem_peak kstat: '$peak'"
	log_must [ $peak -gt 0 ]
	log_must [ $peak -le $cap ]
	clonedup_jstat_gt clonedup_partitions 1
	clonedup_jstat_is errors 0
	clonedup_jstat_is clonedup_applied $((4 * nblk))
	for ((i = 1; i <= 4; i++)); do
		clonedup_check_shared $TESTPOOL a $TESTPOOL c$i "$all"
	done
	clonedup_leakcheck
}

log_assert "a run holds no more memory than its cap"
log_onexit cleanup
log_must save_tunable CLONEDUP_INDEX_FILTER
log_must save_tunable CLONEDUP_SCAN_MEM_MAX

log_must set_tunable32 CLONEDUP_INDEX_FILTER 0
log_must set_tunable64 CLONEDUP_SCAN_MEM_MAX $cap
build_pool
clonedup_run
check_run
log_must zpool destroy $TESTPOOL

log_must set_tunable32 CLONEDUP_INDEX_FILTER 1
build_pool
clonedup_run
clonedup_kstat_gt count_walks 0
check_run

log_pass "a run holds no more memory than its cap"
