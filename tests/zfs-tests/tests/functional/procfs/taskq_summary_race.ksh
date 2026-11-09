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

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Reading /proc/spl/kstat/taskq/summary while taskqs are being created
# and destroyed must not crash.
#
# STRATEGY:
# 1. Start several processes that read the taskq summary in a loop.
# 2. Repeatedly create, export, import and destroy a pool, which creates
#    and destroys many taskqs.
# 3. Stop the readers and check that they all exited cleanly.
#

verify_runnable "global"

typeset -r SUMMARY=/proc/spl/kstat/taskq/summary
typeset -r POOL=testpool_tqsummary
typeset -r VDIR=$TEST_BASE_DIR/taskq_summary_race
typeset -r STOP=$VDIR/stop
typeset -i READERS=4
typeset -i ITERS=10
typeset pids=""

function cleanup
{
	touch $STOP
	for pid in $pids; do
		kill $pid 2>/dev/null
		wait $pid 2>/dev/null
	done
	poolexists $POOL && destroy_pool $POOL
	rm -rf $VDIR
}

log_onexit cleanup

log_assert "Reading $SUMMARY during taskq create/destroy does not crash"

[[ -r $SUMMARY ]] || log_unsupported "$SUMMARY not found"

log_must mkdir -p $VDIR
log_must truncate -s $MINVDEVSIZE $VDIR/vdev
rm -f $STOP

# Readers also stop if the test shell is gone, e.g. killed on timeout.
for i in $(seq $READERS); do
	while [[ ! -e $STOP ]] && kill -0 $$ 2>/dev/null; do
		cat $SUMMARY >/dev/null || exit 1
	done &
	pids="$pids $!"
done

for i in $(seq $ITERS); do
	log_must zpool create -f -O mountpoint=none $POOL $VDIR/vdev
	log_must zpool export $POOL
	log_must zpool import -d $VDIR $POOL
	log_must zpool destroy $POOL
done

touch $STOP
log_must wait_for_children $pids
pids=""

log_pass "Reading $SUMMARY during taskq create/destroy does not crash"
