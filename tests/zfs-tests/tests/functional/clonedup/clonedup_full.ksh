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
#	Duplicates that both predate the last completed run are found
#	only by -f, which indexes every block.
#

verify_runnable "global"

log_assert "zpool clonedup -f finds duplicates among old data"
log_onexit clonedup_cleanup

clonedup_pool_create
clonedup_write /$TESTPOOL/a
clonedup_dup /$TESTPOOL/a /$TESTPOOL/b
clonedup_sync

# Age the pair past a completed run without touching it.
log_must zfs set clonedup=off $TESTPOOL
clonedup_run
log_must zfs inherit clonedup $TESTPOOL
clonedup_check_shared $TESTPOOL a $TESTPOOL b ""

clonedup_run
clonedup_jstat_is clonedup_indexed 0
clonedup_check_shared $TESTPOOL a $TESTPOOL b ""

clonedup_run -f
clonedup_jstat_is clonedup_flags.full true
clonedup_jstat_is clonedup_min_txg 0
clonedup_check_shared $TESTPOOL a $TESTPOOL b "$(clonedup_all_blocks)"
clonedup_leakcheck

log_pass "zpool clonedup -f finds duplicates among old data"
