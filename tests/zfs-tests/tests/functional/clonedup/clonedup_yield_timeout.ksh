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
#	When the apply thread does not let go of a dataset within
#	zfs_clonedup_yield_timeout_ms, a destroy of that dataset fails
#	with EBUSY as it would for any other holder, and succeeds once
#	the run is over.
#

verify_runnable "global"

function cleanup
{
	log_must restore_tunable CLONEDUP_APPLY_TXG_DELAY
	log_must restore_tunable CLONEDUP_YIELD_TIMEOUT_MS
	clonedup_cleanup
}

log_assert "a yield that times out leaves the destroy with EBUSY"
log_onexit cleanup
log_must save_tunable CLONEDUP_APPLY_TXG_DELAY
log_must save_tunable CLONEDUP_YIELD_TIMEOUT_MS

clonedup_pool_create

log_must zfs create $TESTPOOL/dd
clonedup_write /$TESTPOOL/dd/a
clonedup_dup /$TESTPOOL/dd/a /$TESTPOOL/dd/b
clonedup_sync
log_must zfs unmount $TESTPOOL/dd

# the thread sleeps 8 seconds per block while holding the dataset, and
# a waiter gives up after one
log_must set_tunable32 CLONEDUP_APPLY_TXG_DELAY 8000
log_must set_tunable32 CLONEDUP_YIELD_TIMEOUT_MS 1000

log_must zpool clonedup $TESTPOOL
clonedup_wait_applying
sleep 2
typeset -i y0=$(clonedup_kstat yields)
typeset -F t0=$SECONDS
log_mustnot_expect "busy" zfs destroy $TESTPOOL/dd
typeset -F took=$((SECONDS - t0))
log_note "the destroy failed after $took s"
(( took >= 1 )) || log_fail "the destroy did not wait out the yield"
clonedup_kstat_gt yields $y0
log_must datasetexists $TESTPOOL/dd

log_must set_tunable32 CLONEDUP_APPLY_TXG_DELAY 0
log_must zpool wait -t clonedup $TESTPOOL
log_must zfs destroy $TESTPOOL/dd
clonedup_leakcheck

log_pass "a yield that times out leaves the destroy with EBUSY"
