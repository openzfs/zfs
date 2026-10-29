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
#	Conflicting or incomplete command lines are rejected, and the
#	usage shows only the forms the command accepts.
#

verify_runnable "global"

log_assert "zpool clonedup rejects bad option combinations"
log_onexit clonedup_cleanup

clonedup_pool_create
clonedup_write /$TESTPOOL/a
clonedup_dup /$TESTPOOL/a /$TESTPOOL/b
clonedup_sync

# Each of these would start a run if zpool accepted it.
log_mustnot_expect "invalid option combination" \
    zpool clonedup -f -q $TESTPOOL
log_mustnot_expect "invalid option" zpool clonedup -x $TESTPOOL
log_mustnot_expect "missing pool name" zpool clonedup
log_mustnot_expect "no such pool" zpool clonedup nonexistent-pool
# A pool that has never run a scan shows no scan_stats.
typeset scan
scan=$(zpool status -j $TESTPOOL | jq -r --arg p $TESTPOOL \
    '.pools[$p] | has("scan_stats")')
[[ $scan == false ]] || log_fail "a run was recorded: '$scan'"

# The usage offers -s and -p only on their own, since every
# combination of either with another option fails.
typeset usage
usage=$(zpool clonedup -x 2>&1)
log_note "usage: $usage"
print -r -- "$usage" | grep -qe '-[ps].*-[wfqn]' &&
    log_fail "the usage offers -s or -p with another option"
print -r -- "$usage" | grep -q 'clonedup -p | -s ' ||
    log_fail "the usage has no form for -p and -s"

# -s or -p alone acts on this run, so each pair has only its option
# check to fail on.  A -w pair could otherwise wait on the run.
clonedup_hold
log_must zpool clonedup $TESTPOOL
clonedup_wait_running
typeset opts
for opts in "-s -p" "-w -s" "-w -p" "-n -s" "-f -p"; do
	log_mustnot_expect "invalid option combination" \
	    timeout 60 zpool clonedup $opts $TESTPOOL
done
log_must clonedup_is_running
log_must zpool clonedup -p $TESTPOOL
log_must clonedup_is_paused
log_must zpool clonedup -s $TESTPOOL
clonedup_jstat_is state CANCELED
clonedup_release

log_pass "zpool clonedup rejects bad option combinations"
