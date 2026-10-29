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
#	A run interrupted by an export restarts after import and
#	finishes.  The restart does not count as a canceled run.
#

verify_runnable "global"

log_assert "a clonedup run survives export and import"
log_onexit clonedup_cleanup

clonedup_pool_create
clonedup_write /$TESTPOOL/a
clonedup_dup /$TESTPOOL/a /$TESTPOOL/b
clonedup_sync

clonedup_hold
log_must zpool clonedup $TESTPOOL
clonedup_wait_running
log_must zpool export $TESTPOOL
log_must zpool import $TESTPOOL
clonedup_wait_running
log_must eval "zpool status $TESTPOOL | grep -q 'indexing new data'"
log_must [ "$(get_pool_prop feature@clonedup $TESTPOOL)" = "active" ]
clonedup_release

log_must zpool wait -t clonedup $TESTPOOL
clonedup_jstat_is state FINISHED
clonedup_kstat_is canceled_runs 0
clonedup_check_shared $TESTPOOL a $TESTPOOL b "$(clonedup_all_blocks)"
log_must [ "$(get_pool_prop feature@clonedup $TESTPOOL)" = "enabled" ]
clonedup_leakcheck

log_pass "a clonedup run survives export and import"
