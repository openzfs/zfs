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
#	zpool clonedup -s stops a clonedup run and nothing else.  The
#	command reads each pool's status before it sends the stop, and
#	a scrub can replace the run in between.  The kernel checks the
#	stop again when it takes effect and leaves the scrub running.
#
# STRATEGY:
#	1. Start a clonedup run on two pools and hold every scan in
#	   place.
#	2. Delay the writes of the first pool, so its stop takes
#	   seconds to sync.
#	3. Stop both runs with one zpool clonedup -s.  It reads the
#	   status of both pools, then stops them in name order.
#	4. While the first stop waits for its txg, start a scrub on
#	   the second pool.  The scrub replaces the run there.
#	5. Check that the first run was canceled and that the scrub on
#	   the second pool still runs.
#

verify_runnable "global"

typeset vdev=$TEST_BASE_DIR/clonedup_stop_raced.vdev
typeset out=$TEST_BASE_DIR/clonedup_stop_raced.out
typeset stop_pid=""

# Does the scan line of a pool's status match a pattern?
function cd_scan_is # pool pattern
{
	zpool status $1 | grep -q "$2"
}

function cleanup
{
	zinject -c all >/dev/null 2>&1
	[[ -n $stop_pid ]] && wait $stop_pid
	clonedup_release
	if poolexists $TESTPOOL1; then
		zpool scrub -s $TESTPOOL1 >/dev/null 2>&1
		destroy_pool $TESTPOOL1
	fi
	rm -f $vdev $out
	clonedup_cleanup
}

log_assert "zpool clonedup -s leaves a scrub that replaced the run"
log_onexit cleanup

[[ $TESTPOOL < $TESTPOOL1 ]] ||
    log_unsupported "$TESTPOOL does not sort before $TESTPOOL1"

clonedup_pool_create
clonedup_write /$TESTPOOL/a
clonedup_dup /$TESTPOOL/a /$TESTPOOL/b
clonedup_sync

log_must truncate -s $MINVDEVSIZE $vdev
log_must zpool create -f -o feature@clonedup=enabled $TESTPOOL1 $vdev
clonedup_write /$TESTPOOL1/a
clonedup_dup /$TESTPOOL1/a /$TESTPOOL1/b
log_must sync
log_must zpool sync $TESTPOOL1

clonedup_hold
log_must zpool clonedup $TESTPOOL
log_must zpool clonedup $TESTPOOL1
log_must cd_scan_is $TESTPOOL "clonedup.* in progress"
log_must cd_scan_is $TESTPOOL1 "clonedup.* in progress"

# Every write to the first pool takes two seconds, one at a time.
clonedup_delay_writes 2000
zpool clonedup -s $TESTPOOL $TESTPOOL1 >$out 2>&1 &
stop_pid=$!
sleep 1
kill -0 $stop_pid 2>/dev/null ||
    log_fail "the stop ended before the scrub could replace the run"
log_must zpool scrub $TESTPOOL1
log_must is_pool_scrubbing $TESTPOOL1
kill -0 $stop_pid 2>/dev/null ||
    log_fail "the stop ended before the scrub replaced the run"
log_must zinject -c all
wait $stop_pid
stop_pid=""
log_note "zpool clonedup -s: $(<$out)"

log_must cd_scan_is $TESTPOOL "clonedup.* canceled"
log_must is_pool_scrubbing $TESTPOOL1
log_must zpool scrub -s $TESTPOOL1
clonedup_release

log_pass "zpool clonedup -s leaves a scrub that replaced the run"
