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
#	A block clone between two files never deadlocks with an apply
#	batch that is redirecting blocks of both.  The batch takes the
#	destination range locks in apply order and keeps them until it
#	commits.  A clone takes its two range locks in znode address
#	order.  Two runs queue the files' blocks in opposite orders,
#	and the files stay open so their znodes keep their addresses,
#	so one of the runs takes the locks against the clone's order.
#	Each run starts the clone once the first block is locked and
#	the batch is waiting before the second.  The run and the clone
#	must both finish.  The clone needs a lock the batch holds, so
#	it takes at least half a delay.
#

verify_runnable "global"

# ms the apply waits before each block; the window for the clone
typeset -r CD_LO_DELAY=4000
typeset -r CD_LO_WANT=$TEST_BASE_DIR/clonedup_lo_want
typeset -r CD_LO_TOOK=$TEST_BASE_DIR/clonedup_lo_took
typeset hung=""
typeset clone_pid=""

function cleanup
{
	exec 5<&- 6<&-
	log_must restore_tunable CLONEDUP_APPLY_TXG_DELAY
	log_must restore_tunable CLONEDUP_APPLY_THREADS
	rm -f $CD_LO_WANT $CD_LO_TOOK
	if [[ -n $hung ]]; then
		log_note "$hung; $TESTPOOL is left in place"
		return 0
	fi
	clonedup_cleanup
}

# Wait up to $3 seconds for kstat counter $1 to reach $2.
function clonedup_lo_kstat # name value seconds
{
	typeset -i i v

	for ((i = 0; i < $3 * 10; i++)); do
		v=$(clonedup_kstat $1)
		(( v >= $2 )) && return 0
		sleep 0.1
	done
	return 1
}

# Wait up to $2 seconds for background job $1 to exit.
function clonedup_lo_exited # pid seconds
{
	typeset -i i

	for ((i = 0; i < $2 * 10; i++)); do
		kill -0 $1 2>/dev/null || return 0
		sleep 0.1
	done
	return 1
}

# Where the clone and the apply thread are stuck, for the log.
function clonedup_lo_stacks
{
	typeset p

	is_linux || return 0
	for p in $clone_pid ${clone_pid:+$(pgrep -P $clone_pid)} \
	    $(pgrep z_clonedup); do
		log_note "stack of $p" \
		    "($(cat /proc/$p/comm 2>/dev/null)):" \
		    "$(cat /proc/$p/stack 2>/dev/null)"
	done
}

#
# One run.  The batch locks the block of $1 first and then waits
# CD_LO_DELAY before locking the block of $2.  It opens $2 just
# before that wait.  That open is the run's second, so the
# dst_mounted kstat tells the test when to start the clone.
#
function clonedup_lo_race # first second
{
	typeset -i v0 rc
	typeset -F t0 took

	v0=$(clonedup_kstat dst_mounted)
	log_must zpool clonedup $TESTPOOL
	clonedup_lo_kstat dst_mounted $((v0 + 2)) 120 ||
	    log_fail "the run did not reach /$TESTPOOL/$2"

	t0=$SECONDS
	(
		clonefile -f /$TESTPOOL/a /$TESTPOOL/b 0 0 $CD_BS
		rc=$?
		echo $((SECONDS - t0)) > $CD_LO_TOOK
		exit $rc
	) &
	clone_pid=$!

	timeout 120 zpool wait -t clonedup $TESTPOOL
	rc=$?
	if (( rc != 0 )); then
		hung="the run locking $1 first did not finish"
		clonedup_lo_stacks
		log_fail "$hung (zpool wait returned $rc)"
	fi
	if ! clonedup_lo_exited $clone_pid 60; then
		hung="the clone during the run locking $1 first hung"
		clonedup_lo_stacks
		log_fail "$hung"
	fi
	wait $clone_pid
	rc=$?
	clone_pid=""
	log_must [ $rc -eq 0 ]
	took=$(<$CD_LO_TOOK)
	log_note "the clone took $took s"
	(( took >= CD_LO_DELAY / 2000 )) ||
	    log_fail "the clone did not wait for the batch's lock"

	clonedup_jstat_is errors 0
	# The batch clones both blocks, unless the clone held the lock
	# of the second first.  Then the batch commits the first alone
	# and finds the second rewritten by the clone: stale once the
	# clone's txg has synced, dirty before that.
	typeset applied=$(clonedup_jstat clonedup_applied)
	typeset stale=$(clonedup_jstat clonedup_skipped_stale)
	typeset dirty=$(clonedup_jstat clonedup_skipped_dirty)
	log_note "applied $applied, stale $stale, dirty $dirty"
	log_note "busy $(clonedup_jstat clonedup_skipped_busy)," \
	    "policy $(clonedup_jstat clonedup_skipped_policy)," \
	    "shared '$(clonedup_shared $TESTPOOL a $TESTPOOL b)'"
	clonedup_assert_counts "$applied" "$stale" "$dirty"
	log_must [ $applied -ge 1 ]
	log_must [ $((applied + stale + dirty)) -eq 2 ]
	log_must cmp /$TESTPOOL/a $CD_LO_WANT
	log_must cmp /$TESTPOOL/b $CD_LO_WANT
}

# Fill the one block of $1 with block $2 of s.
function clonedup_lo_copy # file blkid
{
	log_must dd if=/$TESTPOOL/s of=/$TESTPOOL/$1 bs=$CD_BS \
	    count=1 skip=$2 conv=notrunc status=none
}

log_assert "a clone between two files never deadlocks with the apply"
log_onexit cleanup
log_must save_tunable CLONEDUP_APPLY_TXG_DELAY
log_must save_tunable CLONEDUP_APPLY_THREADS

clonedup_pool_create

# One worker, one batch, blocks visited in the order of their source.
log_must set_tunable32 CLONEDUP_APPLY_THREADS 1
log_must set_tunable32 CLONEDUP_APPLY_TXG_DELAY $CD_LO_DELAY

#
# The test writes and syncs s first, so s is the older copy and the
# source of both groups.  The apply visits the destination holding
# block 0 of s first.
#
clonedup_write /$TESTPOOL/s 2
clonedup_sync
clonedup_lo_copy a 0
clonedup_lo_copy b 1
clonedup_sync
exec 5< /$TESTPOOL/a
exec 6< /$TESTPOOL/b

# The clone copies a over b, so both end up holding block 0 of s.
log_must dd if=/$TESTPOOL/s of=$CD_LO_WANT bs=$CD_BS count=1 \
    status=none
clonedup_lo_race a b

clonedup_write /$TESTPOOL/s 2
clonedup_sync
clonedup_lo_copy a 1
clonedup_lo_copy b 0
clonedup_sync
log_must dd if=/$TESTPOOL/s of=$CD_LO_WANT bs=$CD_BS count=1 skip=1 \
    status=none
clonedup_lo_race b a

exec 5<&- 6<&-
clonedup_leakcheck

log_pass "a clone between two files never deadlocks with the apply"
