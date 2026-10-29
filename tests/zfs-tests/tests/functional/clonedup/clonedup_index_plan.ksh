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
#	The counting pre-pass is planned from what the last one
#	measured, and the decision does not alternate between runs.
#
# STRATEGY:
#	1. Cap the index small so it needs several partitions, which
#	   is the only shape where a pre-pass can pay.
#	2. Fill the pool mostly with duplicates, so the fraction the
#	   filter would keep is high and a pre-pass does not pay once
#	   that fraction is known.
#	3. Run once with the pre-pass forced, which measures the
#	   fraction, then twice on auto.  A run that declines
#	   measures nothing, so each of the two has to use the
#	   measurement carried forward from run one.  Without it the
#	   decision alternates: on, off, on, off.
#	4. The measured fraction persists in the phys across runs
#	   that did not take a pre-pass.
#

verify_runnable "global"

function cleanup
{
	log_must restore_tunable CLONEDUP_INDEX_FILTER
	log_must restore_tunable CLONEDUP_SCAN_MEM_MAX
	clonedup_cleanup
}

log_assert "the counting pre-pass decision is stable across runs"
log_onexit cleanup
log_must save_tunable CLONEDUP_INDEX_FILTER
log_must save_tunable CLONEDUP_SCAN_MEM_MAX

typeset -ri nblk=200
typeset -ri bs=8192

clonedup_pool_create -O recordsize=8k

# Mostly duplicates.  A high keep fraction is what makes a pre-pass
# stop paying.  A fraction derived from candidates collapses once
# the duplicates are shared, and a planner using it keeps choosing
# a pre-pass that saves nothing.  1000 blocks at about 104 bytes of
# index each need several partitions under a 32 KiB cap, and so do
# the 800 a filter keeps.
clonedup_write /$TESTPOOL/a $nblk $bs
clonedup_dup /$TESTPOOL/a /$TESTPOOL/b
clonedup_dup /$TESTPOOL/a /$TESTPOOL/c
clonedup_dup /$TESTPOOL/a /$TESTPOOL/d
clonedup_write /$TESTPOOL/unique $nblk $bs
clonedup_sync

log_must set_tunable64 CLONEDUP_SCAN_MEM_MAX $((32 * 1024))

# Three full runs.  The first takes the pre-pass on a forced filter
# and measures the keep fraction.  The two after it decide on auto
# from that measurement, and must decline the same way.
typeset -a walks keep
typeset -i run
walks[0]=$(clonedup_kstat count_walks)
clonedup_assert_counts "${walks[0]}"
for run in 1 2 3; do
	if (( run == 1 )); then
		log_must set_tunable32 CLONEDUP_INDEX_FILTER 1
	else
		log_must set_tunable32 CLONEDUP_INDEX_FILTER 2
	fi
	clonedup_run -f
	walks[run]=$(clonedup_kstat count_walks)
	keep[run]=$(clonedup_record keep_pct)
	clonedup_assert_counts "${walks[run]}" "${keep[run]}"
	clonedup_jstat_gt clonedup_partitions 1
	clonedup_jstat_is errors 0
	clonedup_leakcheck
done

typeset -i d1=$((walks[1] - walks[0]))
typeset -i d2=$((walks[2] - walks[1]))
typeset -i d3=$((walks[3] - walks[2]))
log_note "pre-passes per run: $d1 $d2 $d3"
log_note "keep_pct after each run: ${keep[1]} ${keep[2]} ${keep[3]}"
log_must [ $d1 -eq 1 ]
log_must [ ${keep[1]} -gt 0 ]

if (( d2 != d3 )); then
	log_fail "the pre-pass decision alternated: run two took" \
	    "$d2 and run three took $d3. A run that declines" \
	    "records no measurement, so the decision must come" \
	    "from one that was carried forward."
fi
# With the measured fraction a pre-pass does not pay on this data.
log_must [ $d2 -eq 0 ]

# The fraction outlives the run that measured it.  Without it the
# planner models one from candidates and the decision flips.
log_must [ ${keep[2]} -eq ${keep[1]} ]
log_must [ ${keep[3]} -eq ${keep[1]} ]

# The result does not depend on any of this.
clonedup_check_shared $TESTPOOL a $TESTPOOL b \
    "$(clonedup_all_blocks $nblk)"
log_must cmp /$TESTPOOL/a /$TESTPOOL/b
log_must cmp /$TESTPOOL/a /$TESTPOOL/c
log_must cmp /$TESTPOOL/a /$TESTPOOL/d

log_pass "the counting pre-pass decision is stable across runs"
