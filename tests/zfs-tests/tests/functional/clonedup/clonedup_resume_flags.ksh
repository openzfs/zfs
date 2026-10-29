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
#	A request refused because a run is paused names the command
#	that resumes that run, with the run's own flags.  That command
#	resumes the run rather than starting a new one, and the run
#	finishes.  The scan line of zpool status calls the run full or
#	quick while it is paused, running and once it completes.
#
# STRATEGY:
#	1. Pause a held -f run and request a plain run, which is
#	   refused.  The message must name 'zpool clonedup -f'.
#	2. Pause a held -q run and pause it again, which is refused.
#	   The message must name 'zpool clonedup -q', since a plain
#	   request would replace the run.
#	3. Each time, run the named command, check that it resumed the
#	   run without a new scan setup, and let the run finish.
#	   Check the scan line in each state.
#

verify_runnable "global"

# Prints nothing when zpool history fails, so a compare fails too.
function setups
{
	typeset h

	h=$(zpool history -i $TESTPOOL) || return 1
	print -r -- "$h" | grep -c 'scan setup'
}

# Pause the held run, make a request that is refused, then resume
# the run with the command the refusal names.
function check_resume # flag request...
{
	typeset flag=$1
	shift
	typeset out cmd mode=full
	typeset -i rc n

	[[ $flag == -q ]] && mode=quick
	log_must zpool clonedup -p $TESTPOOL
	log_must clonedup_is_paused
	log_must eval "zpool status $TESTPOOL |" \
	    "grep -q 'scan: $mode clonedup paused since'"
	n=$(setups)

	out=$(zpool clonedup "$@" $TESTPOOL 2>&1)
	rc=$?
	log_note "zpool clonedup $* on a paused $flag run:" \
	    "rc $rc: $out"
	(( rc == 1 )) ||
	    log_fail "zpool clonedup $* exited $rc, want 1"
	log_must clonedup_is_paused

	cmd=$(echo "$out" | sed -n \
	    "s/.*use '\(zpool clonedup[^']*\)' to resume.*/\1/p")
	[[ "$cmd" == "zpool clonedup $flag" ]] ||
	    log_fail "refusal names '$cmd'," \
	    "want 'zpool clonedup $flag'"

	log_must $cmd $TESTPOOL
	log_must clonedup_is_running
	log_mustnot clonedup_is_paused
	log_must eval "zpool status $TESTPOOL |" \
	    "grep -q 'scan: $mode clonedup in progress since'"
	log_must [ $(setups) -eq $n ]
	clonedup_jstat_is clonedup_flags.$mode true
}

log_assert "a refused request names the command that resumes the run"
log_onexit clonedup_cleanup

clonedup_pool_create
clonedup_write /$TESTPOOL/a
clonedup_dup /$TESTPOOL/a /$TESTPOOL/b
clonedup_sync

clonedup_hold
log_must zpool clonedup -f $TESTPOOL
clonedup_wait_running
check_resume -f
clonedup_release
log_must zpool wait -t clonedup $TESTPOOL
clonedup_jstat_is state FINISHED
clonedup_jstat_is clonedup_flags.full true
log_must eval "zpool status $TESTPOOL |" \
    "grep -q 'scan: full clonedup completed'"
clonedup_check_shared $TESTPOOL a $TESTPOOL b "$(clonedup_all_blocks)"

clonedup_write /$TESTPOOL/c
clonedup_dup /$TESTPOOL/c /$TESTPOOL/d
clonedup_sync

clonedup_hold
log_must zpool clonedup -q $TESTPOOL
clonedup_wait_running
check_resume -q -p
clonedup_release
log_must zpool wait -t clonedup $TESTPOOL
clonedup_jstat_is state FINISHED
clonedup_jstat_is clonedup_flags.quick true
log_must eval "zpool status $TESTPOOL |" \
    "grep -q 'scan: quick clonedup completed'"
clonedup_check_shared $TESTPOOL c $TESTPOOL d "$(clonedup_all_blocks)"

clonedup_leakcheck

log_pass "a refused request names the command that resumes the run"
