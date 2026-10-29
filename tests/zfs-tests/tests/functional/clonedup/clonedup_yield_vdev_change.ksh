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
#	A mount that the apply makes wait never hangs the pool.  The
#	mount ends its wait without taking spa_namespace_lock while it
#	holds the pool's config lock.  Otherwise a vdev change, which
#	holds spa_namespace_lock while it waits for a txg, and a sync
#	task in that txg, which needs the config lock as writer, wait
#	on the mount and on each other.  The window is narrow, so the
#	test opens it as often as one slow apply allows.
#
# STRATEGY:
#	1. Slow the apply down on an unmounted filesystem, so each
#	   mount finds the apply holding it and waits.
#	2. Delay the pool's writes, so each txg takes a while to sync.
#	3. Mount and unmount the filesystem in one loop, add and
#	   remove a cache device in a second, and set a user property
#	   in a third, so most txgs carry a sync task.
#	4. Wait for the run with a bound.  A hang keeps zpool wait
#	   from returning, and the test fails there.
#	5. Check that the vdev loop ran and that mounts waited on the
#	   apply.
#

verify_runnable "global"

typeset cache=$TEST_BASE_DIR/clonedup_yield_cache
typeset errs=$TEST_BASE_DIR/clonedup_yield_errs
typeset stop=$TEST_BASE_DIR/clonedup_yield_stop
typeset cycles=$TEST_BASE_DIR/clonedup_yield_cycles
typeset pids=""

function cleanup
{
	touch $stop
	if [[ -z $CD_WEDGED_PID ]]; then
		[[ -n $pids ]] && wait $pids
		zinject -c all >/dev/null 2>&1
		poolexists $TESTPOOL &&
		    zpool remove $TESTPOOL $cache >/dev/null 2>&1
	fi
	log_must restore_tunable CLONEDUP_APPLY_TXG_DELAY
	log_must restore_tunable CLONEDUP_APPLY_BLOCKS_PER_TXG
	log_must restore_tunable TXG_TIMEOUT
	rm -f $cache $errs $stop $cycles
	clonedup_cleanup
}

log_assert "a mount that waits on the apply never hangs a vdev change"
log_onexit cleanup
log_must save_tunable CLONEDUP_APPLY_TXG_DELAY
log_must save_tunable CLONEDUP_APPLY_BLOCKS_PER_TXG
log_must save_tunable TXG_TIMEOUT

clonedup_pool_create
typeset -i nblk=128
log_must zfs create $TESTPOOL/um
clonedup_write /$TESTPOOL/um/a $nblk
clonedup_dup /$TESTPOOL/um/a /$TESTPOOL/um/b
clonedup_sync
log_must zfs unmount $TESTPOOL/um
log_must truncate -s $MINVDEVSIZE $cache

log_must set_tunable32 CLONEDUP_APPLY_TXG_DELAY 50
log_must set_tunable32 CLONEDUP_APPLY_BLOCKS_PER_TXG 4
log_must set_tunable32 TXG_TIMEOUT 1

log_must zpool clonedup $TESTPOOL
clonedup_wait_applying
clonedup_delay_writes 20

rm -f $errs $stop
log_must touch $cycles
(
	while [[ ! -f $stop ]]; do
		zfs mount $TESTPOOL/um 2>>$errs ||
		    echo "mount $?" >>$errs
		zfs unmount $TESTPOOL/um 2>>$errs ||
		    echo "unmount $?" >>$errs
	done
) &
pids=$!
(
	while [[ ! -f $stop ]]; do
		zpool add -f $TESTPOOL cache $cache &&
		    zpool remove $TESTPOOL $cache &&
		    echo >>$cycles
	done
) >/dev/null 2>&1 &
pids="$pids $!"
(
	typeset -i n=0
	while [[ ! -f $stop ]]; do
		zfs set user:clonedup=$((n++)) $TESTPOOL
	done
) >/dev/null 2>&1 &
pids="$pids $!"

clonedup_bounded 300 zpool wait -t clonedup $TESTPOOL
log_must touch $stop
wait $pids
pids=""
log_must zinject -c all

typeset -i ncycles=$(wc -l < $cycles)
log_note "vdev changes $ncycles, yields $(clonedup_kstat yields)," \
    "mount loop errors: $(cat $errs 2>/dev/null)"
log_must [ $ncycles -gt 0 ]
clonedup_kstat_gt yields 0
clonedup_jstat_gt clonedup_applied 0

ismounted $TESTPOOL/um || log_must zfs mount $TESTPOOL/um
log_must cmp /$TESTPOOL/um/a /$TESTPOOL/um/b
clonedup_leakcheck

log_pass "a mount that waits on the apply never hangs a vdev change"
