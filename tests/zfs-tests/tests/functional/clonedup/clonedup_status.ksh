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
#	zpool status, zpool status -j, zpool wait and the pool
#	property report a run, and zpool status -j gives the phase by
#	name.
#

verify_runnable "global"

log_assert "status, JSON, wait and last_clonedup_txg report a run"
log_onexit clonedup_cleanup

clonedup_pool_create
clonedup_write /$TESTPOOL/a
clonedup_dup /$TESTPOOL/a /$TESTPOOL/b
clonedup_sync

# wait returns at once when nothing runs
log_must zpool wait -t clonedup $TESTPOOL

clonedup_hold
log_must zpool clonedup $TESTPOOL
clonedup_wait_running
log_must eval \
    "zpool status -j $TESTPOOL | grep -q '\"function\":\"CLONEDUP\"'"
log_must eval "zpool status -j $TESTPOOL |" \
    "grep -q '\"clonedup_phase\":\"INDEX\"'"
zpool wait -t clonedup $TESTPOOL &
typeset waiter=$!
sleep 1
log_must kill -0 $waiter
clonedup_release
log_must wait $waiter

log_must eval "zpool status $TESTPOOL |" \
    "grep -q 'clonedup completed: 8 blocks cloned'"
log_must eval "zpool status $TESTPOOL | grep -q 'with 0 errors on'"
log_must eval \
    "zpool status -j $TESTPOOL | grep -q '\"clonedup_applied\"'"
typeset last=$(clonedup_last_txg)
typeset rlast=$(clonedup_jstat clonedup_last_txg)
typeset rmax=$(clonedup_jstat clonedup_max_txg)
log_note "last_clonedup_txg $last, status -j last $rlast, max $rmax"
clonedup_assert_counts "$last" "$rlast" "$rmax"
log_must [ $last -gt 0 ]
log_must [ $last -eq $rlast ]
log_must [ $last -eq $rmax ]
log_must eval "zpool get last_clonedup_txg $TESTPOOL |" \
    "grep -q last_clonedup_txg"

log_pass "status, JSON, wait and last_clonedup_txg report a run"
