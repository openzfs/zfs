#!/bin/ksh
# SPDX-License-Identifier: CDDL-1.0
#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# http://www.illumos.org/license/CDDL.
#

#
# Copyright (c) 2025 by Klara Inc.
#

#
# Description:
# Verify that mixed gang blocks and copies interact correctly in FDT
#
# Strategy:
# 1. Store a block with copies = 1 in the DDT unganged.
# 2. Add a new entry with copies = 2 that gangs, ensure it doesn't panic
# 3. Store a block with copies = 1 in the DDT ganged.
# 4. Add a new entry with copies = 3 that doesn't gang, ensure that it doesn't panic.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/gang_blocks/gang_blocks.kshlib

log_assert "Verify that mixed gang blocks and copies interact correctly in FDT"

save_tunable DEDUP_LOG_TXG_MAX

function cleanup2
{
	zfs destroy $TESTPOOL/fs1
	zfs destroy $TESTPOOL/fs2
	restore_tunable DEDUP_LOG_TXG_MAX
	cleanup
}

preamble
log_onexit cleanup2

log_must zpool create -f -o ashift=9 -o feature@block_cloning=disabled $TESTPOOL $DISKS
log_must zfs create -o recordsize=64k -o dedup=on $TESTPOOL/fs1
log_must zfs create -o recordsize=64k -o dedup=on -o copies=3 $TESTPOOL/fs2
set_tunable32 DEDUP_LOG_TXG_MAX 1
log_must dd if=/dev/urandom of=/$TESTPOOL/fs1/f1 bs=64k count=1
log_must sync_pool $TESTPOOL
set_tunable32 METASLAB_FORCE_GANGING 20000
set_tunable32 METASLAB_FORCE_GANGING_PCT 100
log_must dd if=/$TESTPOOL/fs1/f1 of=/$TESTPOOL/fs2/f1 bs=64k count=1
log_must sync_pool $TESTPOOL

log_must rm /$TESTPOOL/fs*/f1
log_must sync_pool $TESTPOOL
log_must dd if=/dev/urandom of=/$TESTPOOL/fs1/f1 bs=64k count=1
log_must sync_pool $TESTPOOL
log_must zdb -D $TESTPOOL
set_tunable32 METASLAB_FORCE_GANGING_PCT 0
log_must dd if=/$TESTPOOL/fs1/f1 of=/$TESTPOOL/fs2/f1 bs=64k count=1
log_must sync_pool $TESTPOOL

log_must rm /$TESTPOOL/fs*/f1
log_must sync_pool $TESTPOOL
set_tunable32 METASLAB_FORCE_GANGING_PCT 50
set_tunable32 METASLAB_FORCE_GANGING 40000
log_must dd if=/dev/urandom of=/$TESTPOOL/f1 bs=64k count=1
for i in `seq 1 16`; do
	log_must cp /$TESTPOOL/f1 /$TESTPOOL/fs2/f1
	log_must cp /$TESTPOOL/f1 /$TESTPOOL/fs1/f1
	log_must sync_pool $TESTPOOL
	log_must zdb -D $TESTPOOL
done

log_pass "Verify that mixed gang blocks and copies interact correctly in FDT"
