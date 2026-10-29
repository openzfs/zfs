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
#	When the index exceeds its cap the run splits into partitions
#	and still finds every duplicate.
#

verify_runnable "global"

function cleanup
{
	log_must restore_tunable CLONEDUP_SCAN_MEM_MAX
	clonedup_cleanup
}

log_assert \
    "an oversized index splits into partitions without missing data"
log_onexit cleanup
log_must save_tunable CLONEDUP_SCAN_MEM_MAX

clonedup_pool_create -O recordsize=8k
# 2048 blocks at about 104 bytes each is far over a 32 KiB cap.
log_must dd if=/dev/urandom of=/$TESTPOOL/big bs=8k count=1024 \
    status=none
log_must dd if=/$TESTPOOL/big of=/$TESTPOOL/big2 bs=8k status=none
clonedup_sync

log_must set_tunable64 CLONEDUP_SCAN_MEM_MAX $((32 * 1024))
clonedup_run
clonedup_jstat_gt clonedup_partitions 1
clonedup_jstat_is clonedup_indexed 2048
clonedup_jstat_is clonedup_applied 1024
clonedup_check_shared $TESTPOOL big $TESTPOOL big2 \
    "$(clonedup_all_blocks 1024)"
log_must cmp /$TESTPOOL/big /$TESTPOOL/big2
clonedup_leakcheck

log_pass \
    "an oversized index splits into partitions without missing data"
