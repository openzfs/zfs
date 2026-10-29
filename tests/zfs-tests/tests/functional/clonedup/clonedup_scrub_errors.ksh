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
#	A scrub reports its own errors and none of an earlier
#	clonedup run's.  The clonedup error count belongs to the
#	clonedup status.  Once a scrub is the scan on record, zpool
#	status and zpool status -j show only what the scrub found.
#
# STRATEGY:
#	1. Fail every read of one copy so a clonedup run counts
#	   verify errors, then clear the injection.
#	2. Check that the run reported its errors.
#	3. Scrub the pool, which reads back clean.
#	4. The scrub line and the JSON scan stats show no errors.
#

verify_runnable "global"

function cleanup
{
	zinject -c all >/dev/null 2>&1
	zpool clear $TESTPOOL >/dev/null 2>&1
	clonedup_cleanup
}

log_assert "a scrub does not report an earlier clonedup run's errors"
log_onexit cleanup

clonedup_pool_create

clonedup_write /$TESTPOOL/a
clonedup_dup /$TESTPOOL/a /$TESTPOOL/b
clonedup_sync

# Every data read of b fails, so the verify of every pair fails.
log_must zinject -a -t data -e checksum -T read -f 100 /$TESTPOOL/b
clonedup_run
log_must zinject -c all
log_must zpool clear $TESTPOOL

clonedup_jstat_gt errors 0
log_must eval "zpool status $TESTPOOL |" \
    "grep -qE 'clonedup completed.* with [1-9][0-9]* errors on'"

log_must zpool scrub -w $TESTPOOL

log_note "$(zpool status $TESTPOOL | grep 'scan:')"
log_must eval "zpool status $TESTPOOL |" \
    "grep -q 'scrub repaired .* with 0 errors on'"

typeset json=$(zpool status -j --json-int $TESTPOOL)
typeset func=$(echo "$json" | jq -r --arg p $TESTPOOL \
    '.pools[$p].scan_stats.function')
typeset state=$(echo "$json" | jq -r --arg p $TESTPOOL \
    '.pools[$p].scan_stats.state')
typeset errs=$(echo "$json" | jq -r --arg p $TESTPOOL \
    '.pools[$p].scan_stats.errors')
log_note "scan_stats: function $func, state $state, errors $errs"
log_must [ "$func" = "SCRUB" ]
log_must [ "$state" = "FINISHED" ]
log_must [ "$errs" = "0" ]

clonedup_leakcheck

log_pass "a scrub does not report an earlier clonedup run's errors"
