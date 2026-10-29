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
#	zpool clonedup on a pool with a paused scrub, or with a paused
#	error scrub, exits 1 with a message about the paused scrub and
#	leaves it paused.  zpool scrub -e during a clonedup run,
#	running or paused, exits 1 with a message about the run.
#

verify_runnable "global"

typeset failed=""

function cleanup
{
	zinject -c all >/dev/null 2>&1
	zpool clear $TESTPOOL >/dev/null 2>&1
	clonedup_cleanup
}

# Try every form of zpool clonedup and record each one that is not
# refused with exit status 1 and a message that the scan is paused.
function clonedup_refused # scan
{
	typeset opt out what
	typeset -i rc

	for opt in "" -f -q -n; do
		what="zpool clonedup${opt:+ $opt} on a paused $1"
		out=$(zpool clonedup $opt $TESTPOOL 2>&1)
		rc=$?
		log_note "$what: rc $rc: $out"
		if [[ $out == *@(ASSERT|Assertion|VERIFY)* ]]; then
			failed="$failed; $what hit an assertion"
		elif (( rc != 1 )); then
			failed="$failed; $what exited $rc"
		elif [[ $out != *": $1 is paused"* ]]; then
			failed="$failed; $what did not say so"
		fi
	done
}

# Record a zpool scrub -e that is not refused with exit status 1 and
# a message that the clonedup run is running or paused.
function errorscrub_refused # running|paused
{
	typeset out what="zpool scrub -e on a $1 clonedup run"
	typeset -i rc

	out=$(zpool scrub -e $TESTPOOL 2>&1)
	rc=$?
	log_note "$what: rc $rc: $out"
	if [[ $out == *@(ASSERT|Assertion|VERIFY)* ]]; then
		failed="$failed; $what hit an assertion"
	elif (( rc != 1 )); then
		failed="$failed; $what exited $rc"
	elif [[ $out != *": clonedup is $1"* ]]; then
		failed="$failed; $what did not say so"
	fi
}

log_assert "clonedup and error scrub requests are refused cleanly"
log_onexit cleanup

clonedup_pool_create
clonedup_write /$TESTPOOL/a
clonedup_dup /$TESTPOOL/a /$TESTPOOL/b
clonedup_sync

clonedup_hold
log_must zpool scrub $TESTPOOL
log_must zpool scrub -p $TESTPOOL
log_must is_pool_scrub_paused $TESTPOOL
clonedup_refused scrub
log_must is_pool_scrub_paused $TESTPOOL
log_must zpool scrub -s $TESTPOOL
log_must is_pool_scrub_stopped $TESTPOOL

# An error scrub starts only with entries in the error log.
log_must zpool export $TESTPOOL
log_must zpool import $TESTPOOL
log_must zinject -t data -e checksum -f 100 -am /$TESTPOOL/a
dd if=/$TESTPOOL/a of=/dev/null bs=$CD_BS count=1 2>/dev/null &&
    log_fail "read of /$TESTPOOL/a succeeded under zinject"
clonedup_sync
log_must zinject -c all

log_must zpool scrub -e $TESTPOOL
log_must is_pool_error_scrubbing $TESTPOOL
log_must zpool scrub -p $TESTPOOL
log_must is_pool_error_scrub_paused $TESTPOOL
clonedup_refused "error scrub"
log_must is_pool_error_scrub_paused $TESTPOOL
log_must zpool scrub -s $TESTPOOL
log_must is_pool_error_scrub_stopped $TESTPOOL

log_must zpool clonedup $TESTPOOL
clonedup_wait_running
errorscrub_refused running
log_must zpool clonedup -p $TESTPOOL
log_must clonedup_is_paused
errorscrub_refused paused
log_must zpool clonedup -s $TESTPOOL
clonedup_release

[[ -z $failed ]] ||
    log_fail "a request was not refused cleanly$failed"

log_must zpool clear $TESTPOOL
clonedup_run
clonedup_check_shared $TESTPOOL a $TESTPOOL b "$(clonedup_all_blocks)"
clonedup_leakcheck

log_pass "clonedup and error scrub requests are refused cleanly"
