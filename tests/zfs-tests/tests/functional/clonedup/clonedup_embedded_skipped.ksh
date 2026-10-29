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
#	Blocks small enough to live inside their block pointer have no
#	address to share and are never indexed.
#

verify_runnable "global"

log_assert "embedded blocks are not indexed"
log_onexit clonedup_cleanup

log_must zpool create -f -o feature@clonedup=enabled \
    -o feature@embedded_data=enabled -O compression=on \
    $TESTPOOL $(clonedup_disks)

log_must eval "head -c 100 /dev/zero | tr '\\0' a > /$TESTPOOL/tiny"
log_must dd if=/$TESTPOOL/tiny of=/$TESTPOOL/tiny2 bs=64 status=none
log_must dd if=/$TESTPOOL/tiny of=/$TESTPOOL/tiny3 bs=64 status=none
# a pair of full blocks, which the run does index
clonedup_write /$TESTPOOL/r
clonedup_dup /$TESTPOOL/r /$TESTPOOL/r2
clonedup_sync
log_must eval "zdb -vvvvv $TESTPOOL -O tiny | grep -q EMBEDDED"

clonedup_run
clonedup_jstat_is clonedup_indexed $((2 * CD_BLOCKS))
clonedup_jstat_is clonedup_candidates $CD_BLOCKS
clonedup_jstat_is clonedup_applied $CD_BLOCKS
clonedup_check_shared $TESTPOOL r $TESTPOOL r2 \
    "$(clonedup_all_blocks)"
log_must cmp /$TESTPOOL/tiny /$TESTPOOL/tiny2
log_must cmp /$TESTPOOL/tiny /$TESTPOOL/tiny3
clonedup_leakcheck

log_pass "embedded blocks are not indexed"
