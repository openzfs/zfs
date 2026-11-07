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
#	A zpool detach whose txg sync suspends the pool does not hold
#	spa_namespace_lock while it waits: commands on another pool
#	keep working, zpool clear resumes the pool, and the detach
#	then completes.
#
# STRATEGY:
#	1. Create a three-way mirror and a second pool, and stop
#	   background txg syncs
#	2. Fail every write and every probe of two sides of the mirror
#	3. Detach the third side in the background; its txg sync
#	   suspends the pool
#	4. zpool status of the other pool must return within the timeout
#	5. Clear the injections and the pool; the detach must then
#	   succeed and the pool must no longer be suspended
#
# NOTES:
#	The detached side has no handler: an ENXIO handler on it would
#	make vdev_dtl_required() refuse the detach.  A two-way mirror
#	does not work either, since the remaining side takes over the
#	mirror's guid and its handlers stop matching.
#

verify_runnable "global"

log_assert "A vdev config change does not hold the namespace lock" \
    "while its pool is suspended"

typeset -i TIMEOUT=60
typeset -i dpid=0
typeset DEVDIR=$TEST_BASE_DIR/failmode_vdev_detach
typeset M1=$DEVDIR/m1
typeset M2=$DEVDIR/m2
typeset M3=$DEVDIR/m3
typeset OTHER=$DEVDIR/other

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
		log_fail "$* still running after ${TIMEOUT}s"
	fi
	wait $pid
}

function cleanup
{
	if ((dpid > 0)) && kill -0 $dpid 2>/dev/null; then
		log_note "zpool detach is still running"
	fi
	zinject -c all
	zpool clear $TESTPOOL
	restore_tunable TXG_TIMEOUT
	poolexists $TESTPOOL && destroy_pool $TESTPOOL
	poolexists $TESTPOOL1 && destroy_pool $TESTPOOL1
	rm -rf $DEVDIR
}

log_must save_tunable TXG_TIMEOUT
log_onexit cleanup

log_must mkdir -p $DEVDIR
log_must truncate -s $MINVDEVSIZE $M1 $M2 $M3 $OTHER
log_must set_tunable32 TXG_TIMEOUT 600

log_must zpool create -f $TESTPOOL mirror $M1 $M2 $M3
log_must zpool create -f $TESTPOOL1 $OTHER
log_must zpool sync $TESTPOOL $TESTPOOL1

for dev in $M1 $M2; do
	log_must zinject -d $dev -e io -T write $TESTPOOL
	log_must zinject -d $dev -e nxio -T probe $TESTPOOL
done
log_must [ "$(kstat_pool $TESTPOOL state)" != "SUSPENDED" ]

zpool detach $TESTPOOL $M3 &
dpid=$!

typeset -i waited=0
while [[ "$(kstat_pool $TESTPOOL state)" != "SUSPENDED" ]]; do
	if ! kill -0 $dpid 2>/dev/null; then
		wait $dpid
		log_fail "zpool detach returned $? before the pool suspended"
	fi
	((waited++ >= TIMEOUT)) && log_fail "the pool did not suspend"
	sleep 1
done

log_must bounded zpool status $TESTPOOL1
log_must bounded zinject -c all
log_must bounded zpool clear $TESTPOOL

if ! wait_pid $dpid; then
	log_fail "zpool detach still running ${TIMEOUT}s after zpool clear"
fi
wait $dpid
typeset -i rc=$?
dpid=0

log_note "zpool detach returned $rc"
log_must [ $rc -eq 0 ]
log_must [ "$(kstat_pool $TESTPOOL state)" != "SUSPENDED" ]
if zpool status -P $TESTPOOL | grep -q $M3; then
	log_fail "$M3 is still in $TESTPOOL"
fi

log_pass "A vdev config change does not hold the namespace lock" \
    "while its pool is suspended"
