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
#	zpool status counts the apply from 0 up to the number of
#	destinations.  The index also holds blocks with no partner
#	and the copies kept as sources, and neither counts.  A new
#	run starts the count over.
#
# STRATEGY:
#	1. Write a file and a byte copy of it, and a larger file of
#	   blocks with no partner.
#	2. Start a full run on one worker that sleeps before each
#	   block, and read the apply counts until the run ends.
#	3. Check that no reading counts more than the copy's blocks
#	   or goes back, that some were taken inside the apply, and
#	   that the count ends at all of the copy's blocks.
#	4. Start a run, hold it in its first walk, and check that
#	   its apply counts start at 0.
#

verify_runnable "global"

typeset -ri ndup=32
typeset -ri nuniq=256

function cleanup
{
	log_must restore_tunable CLONEDUP_APPLY_TXG_DELAY
	log_must restore_tunable CLONEDUP_APPLY_THREADS
	clonedup_cleanup
}

# One counter of a zpool status -j --json-int document.
function cd_key # json key
{
	echo "$1" | sed -n 's/.*"'"$2"'":\([0-9][0-9]*\).*/\1/p'
}

log_assert "the apply counts destinations only, from 0 each run"
log_onexit cleanup
log_must save_tunable CLONEDUP_APPLY_TXG_DELAY
log_must save_tunable CLONEDUP_APPLY_THREADS

clonedup_pool_create
clonedup_write /$TESTPOOL/a $ndup
clonedup_dup /$TESTPOOL/a /$TESTPOOL/b
clonedup_write /$TESTPOOL/u $nuniq
clonedup_sync

log_must set_tunable32 CLONEDUP_APPLY_THREADS 1
log_must set_tunable32 CLONEDUP_APPLY_TXG_DELAY 100
log_must zpool clonedup -f $TESTPOOL
clonedup_wait_running

typeset json
typeset -i atotal adone prev=0 inside=0 n=0
while clonedup_is_running; do
	json=$(zpool status -j --json-int $TESTPOOL)
	atotal=$(cd_key "$json" clonedup_apply_total)
	adone=$(cd_key "$json" clonedup_apply_done)
	(( atotal == 0 || atotal == ndup )) ||
	    log_fail "apply total $atotal, want 0 or $ndup"
	(( adone <= atotal )) ||
	    log_fail "apply done $adone above its total $atotal"
	(( adone >= prev )) ||
	    log_fail "apply done went back from $prev to $adone"
	prev=$adone
	(( atotal == ndup && adone < atotal )) && ((inside++))
	(( ++n < 1200 )) ||
	    log_fail "the run took more than 120 seconds"
	sleep 0.1
done
log_must zpool wait -t clonedup $TESTPOOL
log_note "$n readings, $inside of them inside the apply"
log_must [ $inside -gt 0 ]

json=$(zpool status -j --json-int $TESTPOOL)
atotal=$(cd_key "$json" clonedup_apply_total)
adone=$(cd_key "$json" clonedup_apply_done)
typeset -i applied=$(cd_key "$json" clonedup_applied)
log_note "after the run: apply $adone of $atotal, $applied applied"
log_must [ $atotal -eq $ndup ]
log_must [ $adone -eq $ndup ]
log_must [ $applied -eq $ndup ]
clonedup_check_shared $TESTPOOL a $TESTPOOL b \
    "$(clonedup_all_blocks $ndup)"

clonedup_hold
log_must zpool clonedup $TESTPOOL
clonedup_wait_running
json=$(zpool status -j --json-int $TESTPOOL)
atotal=$(cd_key "$json" clonedup_apply_total)
adone=$(cd_key "$json" clonedup_apply_done)
log_note "a new run in its first walk: apply $adone of $atotal"
log_must [ $atotal -eq 0 ]
log_must [ $adone -eq 0 ]
clonedup_release
log_must zpool wait -t clonedup $TESTPOOL
clonedup_leakcheck

log_pass "the apply counts destinations only, from 0 each run"
