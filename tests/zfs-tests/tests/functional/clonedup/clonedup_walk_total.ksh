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
#	On a pool nothing writes to, a clonedup walk never passes its
#	total.  A walk that suspends in many txgs counts each block
#	pointer once, though every resume walks down again to the
#	block it stopped at.
#
# STRATEGY:
#	1. Write many files of two blocks.  Export and import the
#	   pool, delay every read and turn off the scan's prefetch,
#	   so that the walk reads each file's indirect block itself
#	   and waits longer for it than its minimum time per txg.
#	2. Keep a txg sync waited on during a full dry run, so that
#	   the walk suspends at almost every slow read.  Check that
#	   it resumed at least minres times.
#	3. Check that the walk ends at or below its total, and within
#	   4 MiB of it.
#

verify_runnable "global"

# Each file costs the walk one slow read of its indirect block.
typeset -ri nfiles=512
typeset -ri delay_ms=20
typeset -ri minres=100
#
# A quiet pool leaves the walk short of its total by what it cannot
# reach: MOS blocks rewritten after the total was taken, frees the
# last txgs have not returned, and blocks born after the walk's
# bound.
#
typeset -ri margin=$((4 * 1024 * 1024))
typeset waitflag="" waitpid="" json dev over=""
typeset walked1 total1
typeset -i i resumed
typeset r0 r1

function cleanup
{
	cd_waiter_stop
	zinject -c all >/dev/null 2>&1
	log_must restore_tunable SCRUB_MIN_TIME_MS
	log_must restore_tunable NO_SCRUB_PREFETCH
	log_must restore_tunable TXG_TIMEOUT
	clonedup_cleanup
}

# One field of the pool's scan stats in $json.
function cd_scan # key
{
	echo "$json" | jq -r --arg p $TESTPOOL \
	    ".pools[\$p].scan_stats.$1"
}

#
# Wait on txg syncs until cd_waiter_stop, so that the walk finds a
# sync waited on and suspends once its minimum time is up.  zpool
# sync without -f writes nothing.
#
function cd_waiter_start
{
	waitflag=$(mktemp)
	while [[ -f $waitflag ]]; do
		zpool sync $TESTPOOL >/dev/null 2>&1
	done &
	waitpid=$!
}

function cd_waiter_stop
{
	[[ -n $waitpid ]] || return 0
	rm -f $waitflag
	wait $waitpid
	waitpid=""
}

# Fail unless every argument is a count.
function cd_counts # value...
{
	typeset v

	for v in "$@"; do
		[[ $v == +([0-9]) ]] || log_fail "not a count: '$v'"
	done
}

log_assert "a clonedup walk never passes its total on a quiet pool"
log_onexit cleanup
log_must save_tunable SCRUB_MIN_TIME_MS
log_must save_tunable NO_SCRUB_PREFETCH
log_must save_tunable TXG_TIMEOUT

clonedup_pool_create -O recordsize=4k
for ((i = 0; i < nfiles; i++)); do
	clonedup_write /$TESTPOOL/f$i 2 4096
done
clonedup_sync

# A cold cache and slow reads keep the walk waiting on most of the
# indirect and dnode blocks it reads.
log_must zpool export $TESTPOOL
log_must zpool import $TESTPOOL
for dev in $(get_disklist $TESTPOOL); do
	log_must zinject -d $dev -D $delay_ms:1 -T read $TESTPOOL
done
log_must set_tunable32 SCRUB_MIN_TIME_MS 1
log_must set_tunable32 NO_SCRUB_PREFETCH 1
log_must set_tunable32 TXG_TIMEOUT 1
r0=$(clonedup_kstat walk_resumes)
cd_waiter_start
clonedup_run -n -f
cd_waiter_stop
log_must zinject -c all

r1=$(clonedup_kstat walk_resumes)
cd_counts "$r0" "$r1"
((resumed = r1 - r0))
json=$(zpool status -j --json-int $TESTPOOL)
walked1=$(cd_scan examined)
total1=$(cd_scan to_examine)
log_note "suspending walk: walked $walked1 of $total1," \
    "resumed $resumed times"
cd_counts "$walked1" "$total1"
log_must [ "$(cd_scan state)" = "FINISHED" ]
log_must [ $resumed -ge $minres ]
log_must [ $walked1 -gt 0 ]

# Name every walk that passed its total before failing.
(( walked1 <= total1 )) ||
    over="$over, the suspending walk by $((walked1 - total1))"
[[ -z $over ]] || log_fail "walked past the total:${over#,}"
log_must [ $((total1 - walked1)) -le $margin ]
clonedup_leakcheck

log_pass "a clonedup walk never passes its total on a quiet pool"
