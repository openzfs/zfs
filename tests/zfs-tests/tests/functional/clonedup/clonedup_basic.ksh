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
#	Two identical files end up sharing every block after a run.
#
# STRATEGY:
#	1. Write a file and copy it byte for byte.
#	2. Run zpool clonedup and wait for it.
#	3. Every block is shared, the counters agree, content is
#	   intact, last_clonedup_txg advanced, and zdb finds no leak.
#

verify_runnable "global"

log_assert "zpool clonedup shares the blocks of identical files"
log_onexit clonedup_cleanup

clonedup_pool_create
clonedup_write /$TESTPOOL/a
clonedup_dup /$TESTPOOL/a /$TESTPOOL/b
clonedup_sync
clonedup_check_shared $TESTPOOL a $TESTPOOL b ""
log_must [ $(clonedup_last_txg) -eq 0 ]

clonedup_run

clonedup_check_shared $TESTPOOL a $TESTPOOL b "$(clonedup_all_blocks)"
clonedup_jstat_is state FINISHED
clonedup_jstat_is clonedup_applied $CD_BLOCKS
clonedup_jstat_is clonedup_saved \
    $((CD_BLOCKS * $(clonedup_blk_asize $TESTPOOL a)))
clonedup_jstat_is clonedup_saved_snapheld 0
clonedup_jstat_is errors 0
log_must cmp /$TESTPOOL/a /$TESTPOOL/b
log_must [ $(clonedup_last_txg) -gt 0 ]
log_must eval "zpool status $TESTPOOL | grep -q 'clonedup completed'"
log_must [ $(zpool get -Hpo value bclonesaved $TESTPOOL) -gt 0 ]
clonedup_leakcheck

log_pass "zpool clonedup shares the blocks of identical files"
