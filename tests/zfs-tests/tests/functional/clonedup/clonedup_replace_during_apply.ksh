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
#	A run torn down while its apply workers hold candidates leaves
#	the next run alone.  A full run that replaces a default one,
#	and a default run started right after a cancel, both finish,
#	keep their counters in range, and report as applied exactly
#	the clones the kstat counted.
#

verify_runnable "global"

typeset -i nblk=8
typeset -i hold_ms=5000
typeset -i wait_secs=60

function cleanup
{
	log_must restore_tunable CLONEDUP_APPLY_TXG_DELAY
	log_must restore_tunable CLONEDUP_APPLY_THREADS
	clonedup_cleanup
}

# A count taken below zero wraps to twenty digits.
function check_sane # what value
{
	log_note "$1 = '$2'"
	[[ $2 == +([0-9]) ]] || log_fail "$1 is not a number: '$2'"
	(( ${#2} <= 9 )) || log_fail "$1 is out of range: $2"
}

function fill_pool
{
	clonedup_pool_create
	clonedup_write /$TESTPOOL/a $nblk
	clonedup_dup /$TESTPOOL/a /$TESTPOOL/b
	clonedup_sync
}

# Start a default run and return once each worker sleeps inside a
# candidate it has taken from the apply tree.
function start_held
{
	log_must set_tunable32 CLONEDUP_APPLY_TXG_DELAY $hold_ms
	log_must zpool clonedup $TESTPOOL
	clonedup_wait_applying
	sleep 1
}

function check_run # clones_before full
{
	typeset -i before=$1
	typeset full=$2
	typeset key
	typeset -i clones

	log_must timeout $wait_secs zpool wait -t clonedup $TESTPOOL

	for key in clonedup_indexed clonedup_groups \
	    clonedup_candidates clonedup_applied clonedup_saved \
	    clonedup_saved_snapheld clonedup_skipped \
	    clonedup_skipped_stale clonedup_skipped_dirty \
	    clonedup_skipped_differs clonedup_skipped_busy \
	    clonedup_skipped_policy errors clonedup_apply_total \
	    clonedup_apply_done; do
		check_sane $key "$(clonedup_jstat $key)"
	done
	log_must [ "$(clonedup_jstat clonedup_apply_done)" -eq \
	    "$(clonedup_jstat clonedup_apply_total)" ]
	log_must [ "$(clonedup_jstat clonedup_applied)" -eq $nblk ]

	clonedup_jstat_is state FINISHED
	clonedup_jstat_is clonedup_applied $nblk
	clonedup_jstat_is errors 0
	clonedup_jstat_is clonedup_flags.full $full

	clones=$(( $(clonedup_kstat clones) - before ))
	log_note "clones counted by the kstat: $clones"
	log_must [ $clones -eq $nblk ]
	clonedup_check_shared $TESTPOOL a $TESTPOOL b \
	    "$(clonedup_all_blocks $nblk)"
	log_must cmp /$TESTPOOL/a /$TESTPOOL/b
	clonedup_leakcheck
}

log_assert "a run torn down in its apply leaves the next run alone"
log_onexit cleanup
log_must save_tunable CLONEDUP_APPLY_TXG_DELAY
log_must save_tunable CLONEDUP_APPLY_THREADS
log_must set_tunable32 CLONEDUP_APPLY_THREADS 2

# -f replaces the held run inside one sync task.
fill_pool
typeset -i before=$(clonedup_kstat clones)
start_held
log_must zpool clonedup -f $TESTPOOL
log_must set_tunable32 CLONEDUP_APPLY_TXG_DELAY 0
check_run $before true
log_must zpool destroy -f $TESTPOOL

# A cancel, then a new run while the workers still sleep.
fill_pool
before=$(clonedup_kstat clones)
start_held
log_must zpool clonedup -s $TESTPOOL
log_must set_tunable32 CLONEDUP_APPLY_TXG_DELAY 0
log_must zpool clonedup $TESTPOOL
check_run $before false

log_pass "a run torn down in its apply leaves the next run alone"
