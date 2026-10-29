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
#	A pool imported read-only refuses a clonedup run.  Imported
#	normally again, the run works and the pool is intact.
#

verify_runnable "global"

log_assert "a read-only pool refuses clonedup"
log_onexit clonedup_cleanup

clonedup_pool_create
typeset all="$(clonedup_all_blocks)"
clonedup_write /$TESTPOOL/a
clonedup_dup /$TESTPOOL/a /$TESTPOOL/b
clonedup_sync

log_must_busy zpool export $TESTPOOL
log_must zpool import -o readonly=on $TESTPOOL
log_mustnot zpool clonedup $TESTPOOL
log_mustnot zpool clonedup -n $TESTPOOL
log_mustnot clonedup_is_running
log_must [ $(zpool get -Hpo value bcloneused $TESTPOOL) -eq 0 ]

log_must_busy zpool export $TESTPOOL
log_must zpool import $TESTPOOL
clonedup_check_shared $TESTPOOL a $TESTPOOL b ""
clonedup_run
clonedup_check_shared $TESTPOOL a $TESTPOOL b "$all"
log_must cmp /$TESTPOOL/a /$TESTPOOL/b
clonedup_leakcheck

log_pass "a read-only pool refuses clonedup"
