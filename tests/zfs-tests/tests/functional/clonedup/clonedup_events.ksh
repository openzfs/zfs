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
#	A clonedup run posts its own events at the points where a
#	scrub posts the scrub ones: clonedup_start, clonedup_paused,
#	clonedup_resume, clonedup_abort and clonedup_finish.  It posts
#	no scrub or resilver event, and a dry run posts the same
#	events as a real run.  A run that a scrub or another run
#	replaces posts no event of its own, as a scrub that a
#	resilver replaces posts none, and the restart after an
#	import posts nothing.
#
# STRATEGY:
#	1. Run to completion and check for clonedup_start, then
#	   clonedup_finish.
#	2. Run a dry run to completion and check for the same two.
#	3. Hold a run in its first walk.  Pause it, resume it and stop
#	   it with zpool clonedup -s, and check the one event each
#	   step posts.
#	4. Stop a held run with zpool scrub -s and check for
#	   clonedup_abort.
#	5. Start a scrub over a held run and check for scrub_start
#	   alone, then for scrub_finish once the scrub completes.
#	6. Start a real run over a held dry run and check for two
#	   clonedup_start events.
#	7. Export and import the pool under a held run and let the
#	   restart go by.  Check that it posted nothing, then release
#	   the run and check for clonedup_finish.
#	Each check reads every scan event posted since the one before,
#	so a scrub or resilver event in its place fails it.
#

verify_runnable "global"

#
# The scan events posted for the pool since the log was cleared, in
# order and without the sysevent.fs.zfs. prefix: every scrub_*,
# resilver_*, errorscrub_* and clonedup_* class.
#
function ev_scan
{
	zpool events -H $TESTPOOL | awk '{
		n = split($NF, c, ".")
		if (n == 4 && c[1] == "sysevent" &&
		    c[4] ~ /^(scrub|resilver|errorscrub|clonedup)_/) {
			s = s sep c[4]
			sep = " "
		}
	} END { print s }'
}

# Check that the scan events since the log was cleared are the ones
# named, in order, then clear the log.
function ev_check # when [event]...
{
	typeset when=$1 got

	shift
	got=$(ev_scan)
	log_note "$when: scan events '$got'"
	[[ $got == "$*" ]] ||
	    log_fail "$when: scan events '$got', expected '$*'"
	log_must zpool events -c
}

function cleanup
{
	clonedup_release
	clonedup_cleanup
}

log_assert "a clonedup run posts its own events and no scrub event"
log_onexit cleanup

clonedup_pool_create
clonedup_write /$TESTPOOL/a
clonedup_dup /$TESTPOOL/a /$TESTPOOL/b
clonedup_sync
log_must zpool events -c

clonedup_run
ev_check "run" clonedup_start clonedup_finish
clonedup_run -n
ev_check "dry run" clonedup_start clonedup_finish

clonedup_hold
log_must zpool clonedup $TESTPOOL
clonedup_wait_running
ev_check "start" clonedup_start
log_must zpool clonedup -p $TESTPOOL
log_must clonedup_is_paused
ev_check "pause" clonedup_paused
log_must zpool clonedup $TESTPOOL
log_mustnot clonedup_is_paused
ev_check "resume" clonedup_resume
log_must zpool clonedup -s $TESTPOOL
log_mustnot clonedup_is_running
ev_check "zpool clonedup -s" clonedup_abort

log_must zpool clonedup $TESTPOOL
clonedup_wait_running
ev_check "second start" clonedup_start
log_must zpool scrub -s $TESTPOOL
log_mustnot clonedup_is_running
ev_check "zpool scrub -s" clonedup_abort

log_must zpool clonedup $TESTPOOL
clonedup_wait_running
ev_check "start before the scrub" clonedup_start
log_must zpool scrub $TESTPOOL
log_must is_pool_scrubbing $TESTPOOL
ev_check "scrub over the run" scrub_start
clonedup_release
log_must zpool wait -t scrub $TESTPOOL
ev_check "scrub done" scrub_finish

clonedup_hold
log_must zpool clonedup -n $TESTPOOL
clonedup_wait_running
log_must zpool clonedup $TESTPOOL
ev_check "run over a dry run" clonedup_start clonedup_start
log_must zpool clonedup -s $TESTPOOL
ev_check "stop the run" clonedup_abort

log_must zpool clonedup $TESTPOOL
clonedup_wait_running
ev_check "start before the export" clonedup_start
log_must zpool export $TESTPOOL
log_must zpool import $TESTPOOL
typeset -i i
for ((i = 0; i < 5; i++)); do
	log_must zpool sync $TESTPOOL
	sleep 1
done
log_must clonedup_is_running
ev_check "import"
clonedup_release
log_must zpool wait -t clonedup $TESTPOOL
ev_check "run done after the import" clonedup_finish

log_pass "a clonedup run posts its own events and no scrub event"
