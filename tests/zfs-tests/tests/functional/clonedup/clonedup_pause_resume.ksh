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
#	A run can be paused, resumed and stopped, and status reports
#	each state.
#

verify_runnable "global"

log_assert "zpool clonedup -p, resume and -s work"
log_onexit clonedup_cleanup

clonedup_pool_create
clonedup_write /$TESTPOOL/a
clonedup_dup /$TESTPOOL/a /$TESTPOOL/b
clonedup_sync

clonedup_hold
log_must zpool clonedup $TESTPOOL
clonedup_wait_running
log_must eval "zpool status $TESTPOOL | grep -q 'indexing new data'"

log_must zpool clonedup -p $TESTPOOL
log_must clonedup_is_paused
log_mustnot zpool clonedup -p $TESTPOOL
log_must zpool clonedup $TESTPOOL
log_must clonedup_is_running
log_mustnot clonedup_is_paused

log_must zpool clonedup -s $TESTPOOL
clonedup_jstat_is state CANCELED
log_must eval "zpool status $TESTPOOL | grep -q 'clonedup canceled'"
log_must [ $(clonedup_last_txg) -eq 0 ]
clonedup_release

clonedup_run
clonedup_check_shared $TESTPOOL a $TESTPOOL b "$(clonedup_all_blocks)"
clonedup_leakcheck

log_pass "zpool clonedup -p, resume and -s work"
