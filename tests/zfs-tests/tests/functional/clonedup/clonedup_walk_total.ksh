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
#	block it stopped at.  The last partition's walk counts the
#	pointers an earlier partition redirected onto blocks that a
#	snapshot holds, and its total holds them too.
#
# STRATEGY:
#	1. Write many files of two blocks.  Export and import the
#	   pool, delay every read and turn off the scan's prefetch,
#	   so that the walk reads each file's indirect block itself
#	   and waits longer for it than its minimum time per txg.
#	2. Keep a txg sync waited on during a full dry run, so that
#	   the walk suspends at almost every slow read.  Check that
#	   it resumed at least minres times.
#	3. Recreate the pool.  Write a file and snapshot it, then
#	   copy it, snapshot the copy and write one more block.
#	4. Run a full dry run uncapped, then a full clonedup under an
#	   index cap of two thirds of the mem_peak the dry run left.
#	   Check that the run had more than one partition, and that
#	   it redirected every block of the copy, all of them held by
#	   the second snapshot.
#	5. Check that each walk ends at or below its total, and within
#	   4 MiB of it.
#

verify_runnable "global"

# Each file costs the walk one slow read of its indirect block.
typeset -ri nfiles=512
typeset -ri delay_ms=20
typeset -ri minres=100
# blocks in the file the second run copies
typeset -ri n=256
#
# A quiet pool leaves the walk short of its total by what it cannot
# reach: MOS blocks rewritten after the total was taken, frees the
# last txgs have not returned, and blocks born after the walk's
# bound.
#
typeset -ri margin=$((4 * 1024 * 1024))
typeset waitflag="" waitpid="" json dev over=""
typeset walked1 total1 walked2 total2 part parts applied held
typeset -i i resumed asz cap
typeset peak r0 r1

function cleanup
{
	cd_waiter_stop
	zinject -c all >/dev/null 2>&1
	log_must restore_tunable SCRUB_MIN_TIME_MS
	log_must restore_tunable NO_SCRUB_PREFETCH
	log_must restore_tunable TXG_TIMEOUT
	log_must restore_tunable CLONEDUP_SCAN_MEM_MAX
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
log_must save_tunable CLONEDUP_SCAN_MEM_MAX

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

destroy_pool $TESTPOOL
clonedup_pool_create
clonedup_write /$TESTPOOL/a $n
clonedup_sync
# a's blocks are the older and held by s1, so they are the sources
log_must zfs snapshot $TESTPOOL@s1
clonedup_dup /$TESTPOOL/a /$TESTPOOL/h
clonedup_sync
log_must zfs snapshot $TESTPOOL@s2
# written since s2, so the run may still rewrite h
clonedup_write /$TESTPOOL/n 1
clonedup_sync
asz=$(clonedup_blk_asize $TESTPOOL a)
log_must [ $asz -gt 0 ]

# An uncapped dry run indexes the same entries.  Two thirds of the
# most memory it held cannot hold them all, but holds the half that
# stays in the first partition, so the capped run splits.
clonedup_run -n -f
peak=$(clonedup_kstat mem_peak)
log_note "uncapped mem_peak '$peak'"
[[ $peak == +([0-9]) ]] || log_fail "no mem_peak kstat: '$peak'"
log_must [ $peak -gt 0 ]
((cap = peak * 2 / 3))
log_must set_tunable64 CLONEDUP_SCAN_MEM_MAX $cap
clonedup_run -f

json=$(zpool status -j --json-int $TESTPOOL)
walked2=$(cd_scan examined)
total2=$(cd_scan to_examine)
part=$(cd_scan clonedup_partition)
parts=$(cd_scan clonedup_partitions)
applied=$(cd_scan clonedup_applied)
held=$(cd_scan clonedup_saved_snapheld)
log_note "partitioned walk: walked $walked2 of $total2 in partition" \
    "$part of $parts, applied $applied, $held held by snapshots"
cd_counts "$walked2" "$total2" "$part" "$parts" "$applied" "$held"
log_must [ "$(cd_scan state)" = "FINISHED" ]
log_must [ $parts -ge 2 ]
log_must [ $((part + 1)) -eq $parts ]
log_must [ $applied -eq $n ]
log_must [ $held -eq $((n * asz)) ]
log_must [ $walked2 -gt 0 ]
clonedup_check_shared $TESTPOOL a $TESTPOOL h \
    "$(clonedup_all_blocks $n)"

# Name every walk that passed its total before failing.
(( walked1 <= total1 )) ||
    over="$over, the suspending walk by $((walked1 - total1))"
(( walked2 <= total2 )) ||
    over="$over, the partitioned walk by $((walked2 - total2))"
[[ -z $over ]] || log_fail "walked past the total:${over#,}"
log_must [ $((total1 - walked1)) -le $margin ]
log_must [ $((total2 - walked2)) -le $margin ]
clonedup_leakcheck

log_pass "a clonedup walk never passes its total on a quiet pool"
