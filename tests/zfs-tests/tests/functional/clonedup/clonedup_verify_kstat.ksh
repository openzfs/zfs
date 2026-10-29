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
#	A match under a checksum strong enough for dedup is cloned
#	without reading either block, as dedup and FIDEDUPERANGE
#	trust such a match.  Under a weaker checksum every candidate
#	costs two reads.  The kstat counts both.
#

verify_runnable "global"

log_assert "a strong checksum match skips the verify reads"
log_onexit clonedup_cleanup

clonedup_pool_create
log_must zfs create -o checksum=sha256 $TESTPOOL/strong
typeset all="$(clonedup_all_blocks)"

clonedup_write /$TESTPOOL/strong/a
clonedup_dup /$TESTPOOL/strong/a /$TESTPOOL/strong/b
clonedup_sync

clonedup_run
clonedup_check_shared $TESTPOOL/strong a $TESTPOOL/strong b "$all"
clonedup_kstat_is verify_reads 0
clonedup_kstat_is clones $CD_BLOCKS

clonedup_write /$TESTPOOL/c
clonedup_dup /$TESTPOOL/c /$TESTPOOL/d
clonedup_sync

clonedup_run
clonedup_check_shared $TESTPOOL c $TESTPOOL d "$all"
clonedup_kstat_is verify_reads $((2 * CD_BLOCKS))
clonedup_kstat_is verify_bytes $((2 * CD_BLOCKS * CD_BS))
clonedup_kstat_is clones $((2 * CD_BLOCKS))
clonedup_kstat_gt dst_mounted 0
clonedup_kstat_is dst_owned 0
clonedup_kstat_is dst_zvol 0
clonedup_leakcheck

log_pass "a strong checksum match skips the verify reads"
