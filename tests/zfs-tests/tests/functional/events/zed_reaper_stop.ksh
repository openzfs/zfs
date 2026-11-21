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
# Copyright (c) 2026 by Will Rouesnel.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#	ZED exits on SIGTERM even when its zedlet reaper thread has not
#	reached its loop yet.
#
#	ZED starts the reaper on the first event.  zed_exec_fini() stops it
#	by setting a stop flag and sending it SIGCHLD, then joins it.  The
#	reaper cleared that flag itself as its loop started, so a stop
#	request sent before then was lost: the reaper waited for SIGCHLD
#	forever and ZED hung in pthread_join() on exit (or on an -I
#	reinitialisation).
#
# STRATEGY:
#	1. Start ZED (two job slots, so the main thread can launch both
#	   zedlets of an event without the reaper) with the test-only delay
#	   ZED_TEST_REAPER_START_DELAY_MS, which holds a new reaper before
#	   its loop.
#	2. Post one history event, so ZED starts the reaper and launches
#	   the event's zedlets.
#	3. Check that the reaper has reaped nothing yet (it is still held),
#	   and send ZED SIGTERM.
#	4. Check that ZED exits once the hold ends, and logs "Exiting".
#

verify_runnable "global"

START_DELAY_MS=5000
PROP="org.openzfs:reaper_stop"

function cleanup
{
	typeset pids=$(zed_check)

	# A ZED that lost the stop request ignores SIGTERM.
	[[ -n "$pids" ]] && kill -9 $pids 2>/dev/null
	log_must zed_stop
	zfs inherit $PROP $TESTPOOL
}

log_assert "ZED stops a zedlet reaper that has not started its loop"
log_onexit cleanup

[[ -z "$(zed_check)" ]] || log_fail "ZED already running - $(zed_check)"

# No queued event may start the reaper before ours.
log_must zpool events -c

# As zed_start, with two job slots; only ZED's environment gets the delay.
export ZED_TEST_REAPER_START_DELAY_MS=$START_DELAY_MS
log_must truncate -s 0 $ZED_DEBUG_LOG
log_must eval "zed -vF -d $ZEDLET_DIR -P $PATH" \
    "-s $ZEDLET_DIR/state -j 2 2>$ZED_LOG &"
unset ZED_TEST_REAPER_START_DELAY_MS

typeset zedpid=""
for i in $(seq 1 50); do
	zedpid=$(pgrep -o -x 'zed|lt-zed') &&
	    grep -q '^Processing events since' $ZED_LOG && break
	sleep 0.1
done
[[ -n "$zedpid" ]] || log_fail "ZED is not running"
grep -q '^Processing events since' $ZED_LOG ||
    log_fail "ZED is not processing events"

log_must zfs set $PROP=1 $TESTPOOL
for i in $(seq 1 50); do
	(( $(grep -c '^Invoking ' $ZED_LOG) >= 2 )) && break
	sleep 0.1
done
(( $(grep -c '^Invoking ' $ZED_LOG) >= 2 )) ||
    log_fail "ZED launched no zedlets for the event"
log_note "$(grep '^Invoking ' $ZED_LOG)"

# The reaper is still held: it hasn't reaped the zedlets.
grep -q '^Finished ' $ZED_LOG &&
    log_fail "the reaper ran before SIGTERM; the delay did not hold it"
log_must kill $zedpid

SECONDS=0
while [[ -n "$(zed_check)" ]] && (( SECONDS < START_DELAY_MS / 1000 + 10 ));
do
	sleep 1
done
if [[ -n "$(zed_check)" ]]; then
	for t in /proc/$zedpid/task/*; do
		log_note "thread $(<$t/comm): $(<$t/wchan)"
	done
	log_fail "ZED ignored SIGTERM for $SECONDS s"
fi
grep -q '^Exiting' $ZED_LOG || log_fail "ZED did not log \"Exiting\""

log_pass "ZED stops a zedlet reaper that has not started its loop"
