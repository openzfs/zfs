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
# Copyright (c) 2026 by the OpenZFS project.  All rights reserved.
#
. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#	zpool initialize and zpool trim wait for their state change to
#	sync, and for the threads they stop, without holding
#	spa_namespace_lock: when that sync suspends the pool, commands on
#	another pool keep working, zpool clear resumes the pool, and the
#	command then completes.
#
# STRATEGY:
#	1. Create a pool to initialize, a pool to TRIM and a third pool,
#	   with no mounted dataset, and stop background txg syncs
#	2. Initialize the first pool to completion
#	3. Fail every write and every probe of its vdev, and uninitialize
#	   it in the background: the state change dirties a txg, and the
#	   command's txg sync suspends the pool
#	4. zpool status of the third pool and of the suspended pool,
#	   zinject -c and zpool clear must return within the timeout; the
#	   command must then succeed
#	5. Start a rate-limited TRIM of the second pool, fail every write
#	   and every probe of its vdev, and suspend the TRIM in the
#	   background: the TRIM thread syncs the state change on its way
#	   out, which suspends the pool while the command waits for it
#	6. As in 4
#
# NOTES:
#	Without the probe failure, a failed write is reallocated and
#	retried forever instead of suspending the pool.  The TRIM runs at
#	1 MiB/s on an 8 GiB sparse vdev, whose metaslabs are split into
#	128 MiB TRIM extents: the thread waits about two minutes between
#	extents, so it does not sync a txg of its own before the command.
#	Without the fix, the namespace lock stays held until a reboot:
#	every zpool command on the system blocks, and the cleanup then
#	leaves the pools as they are.
#

verify_runnable "global"

log_assert "zpool initialize and zpool trim do not hold the namespace" \
    "lock while they wait for a suspended pool"

typeset -i TIMEOUT=60
typeset -i cpid=0
typeset -i blocked=0
typeset DEVDIR=$TEST_BASE_DIR/failmode_initialize_trim
typeset VDEV=$DEVDIR/vdev
typeset TRIMVDEV=$DEVDIR/trimvdev
typeset OTHER=$DEVDIR/other
typeset OUT=$DEVDIR/cmd.out

#
# Wait up to TIMEOUT seconds for a process to exit.
#
function wait_pid # pid
{
	typeset -i waited=0

	while kill -0 $1 2>/dev/null; do
		((waited++ >= TIMEOUT)) && return 1
		sleep 1
	done
	return 0
}

#
# Run a command and give it TIMEOUT seconds; fail if it is still
# running then.
#
function bounded # cmd [args]
{
	"$@" &
	typeset -i pid=$!

	if ! wait_pid $pid; then
		blocked=1
		log_fail "$* still running after ${TIMEOUT}s"
	fi
	wait $pid
}

#
# Fail every write and every probe of the pool's vdev, run the command
# in the background until it suspends the pool, then check that the
# other pool and the suspended one answer, that zpool clear resumes the
# pool and that the command then succeeds.
#
function suspend_during # pool vdev cmd [args]
{
	typeset pool=$1
	typeset vdev=$2
	shift 2

	log_must zpool sync $pool
	log_must zinject -d $vdev -e io -T write $pool
	log_must zinject -d $vdev -e nxio -T probe $pool
	log_must [ "$(kstat_pool $pool state)" != "SUSPENDED" ]

	"$@" > $OUT 2>&1 &
	cpid=$!

	typeset -i waited=0
	while [[ "$(kstat_pool $pool state)" != "SUSPENDED" ]]; do
		if ! kill -0 $cpid 2>/dev/null; then
			wait $cpid
			log_fail "$* returned $? before the pool suspended:" \
			    "$(<$OUT)"
		fi
		((waited++ >= TIMEOUT)) && log_fail "the pool did not suspend"
		sleep 1
	done

	log_must bounded zpool status $TESTPOOL1
	log_must bounded zpool status $pool
	log_must bounded zinject -c all
	log_must bounded zpool clear $pool
	if ! wait_pid $cpid; then
		blocked=1
		log_fail "$* still running ${TIMEOUT}s after zpool clear"
	fi
	wait $cpid
	typeset -i rc=$?
	cpid=0
	log_note "$* returned $rc: $(<$OUT)"
	log_must [ $rc -eq 0 ]
	log_must [ "$(kstat_pool $pool state)" != "SUSPENDED" ]
}

function cleanup
{
	restore_tunable TXG_TIMEOUT
	if ((blocked != 0)); then
		log_note "commands are blocked: leaving the pools"
		return
	fi
	if ((cpid > 0)) && kill -0 $cpid 2>/dev/null; then
		log_note "the command is still running"
	fi
	zinject -c all
	for pool in $TESTPOOL $TESTPOOL2 $TESTPOOL1; do
		if poolexists $pool; then
			zpool clear $pool
			destroy_pool $pool
		fi
	done
	rm -rf $DEVDIR
}

log_must save_tunable TXG_TIMEOUT
log_onexit cleanup

log_must mkdir -p $DEVDIR
log_must truncate -s $MINVDEVSIZE $VDEV $OTHER
log_must truncate -s 8G $TRIMVDEV
log_must set_tunable32 TXG_TIMEOUT 600

log_must zpool create -f -o failmode=wait -O mountpoint=none $TESTPOOL $VDEV
log_must zpool create -f -o failmode=wait -O mountpoint=none \
    $TESTPOOL2 $TRIMVDEV
log_must zpool create -f $TESTPOOL1 $OTHER

log_must zpool initialize -w $TESTPOOL
log_must eval "zpool status -i $TESTPOOL | grep -q 'initialized, completed'"
suspend_during $TESTPOOL $VDEV zpool initialize -u $TESTPOOL
log_must eval "zpool status -i $TESTPOOL | grep -q 'uninitialized'"

log_must zpool trim -r 1M $TESTPOOL2
log_must eval "zpool status -t $TESTPOOL2 | grep -q 'trimmed, started'"
suspend_during $TESTPOOL2 $TRIMVDEV zpool trim -s $TESTPOOL2
log_must eval "zpool status -t $TESTPOOL2 | grep -q 'trimmed, suspended'"

log_pass "zpool initialize and zpool trim do not hold the namespace" \
    "lock while they wait for a suspended pool"
