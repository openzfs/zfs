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
#	A source punch frees nothing when a snapshot holds the block
#	by the time the punch syncs.  The run asks again in the
#	punch's transaction, so a snapshot taken after the first test
#	turns the punch into a stale skip.
#
# STRATEGY:
#	1. Write an all-zero block to a/s, then one to a/d.  a/s is
#	   the older copy, so it is the group's source.
#	2. Allow one block per txg and keep txgs open, so that the
#	   source's punch, which follows a/d's punch in the same txg,
#	   waits for the next txg after its first test.
#	3. Start a run and wait for a/d's punch.
#	4. Snapshot a while the source's punch waits.  The snapshot
#	   syncs first and holds the source block.
#	5. Check that the source's punch counted as a stale skip, that
#	   a/s still holds its block and that a/d is a hole.
#

verify_runnable "global"

function cleanup
{
	log_must restore_tunable TXG_TIMEOUT
	log_must restore_tunable CLONEDUP_APPLY_BLOCKS_PER_TXG
	clonedup_cleanup
}

# How many of a file's blocks hold data rather than a hole.
function zw_data_blocks # ds file
{
	zdb -vvvvv $1 -O $2 | awk '/ L0 / && /L\/[0-9a-f]+P/ { n++ }
	    END { print n + 0 }'
}

log_assert "a source punch that a snapshot holds counts as a skip"
log_onexit cleanup
log_must save_tunable TXG_TIMEOUT
log_must save_tunable CLONEDUP_APPLY_BLOCKS_PER_TXG

clonedup_pool_create
log_must zfs create $TESTPOOL/a
log_must dd if=/dev/zero of=/$TESTPOOL/a/s bs=$CD_BS count=1 \
    status=none
clonedup_sync
log_must dd if=/dev/zero of=/$TESTPOOL/a/d bs=$CD_BS count=1 \
    status=none
clonedup_sync

# With one block per txg, the source's punch waits for the txg
# after a/d's punch, and with txgs kept open only the snapshot
# below ends a/d's txg.
typeset txg_timeout=$(get_tunable TXG_TIMEOUT)
log_must set_tunable32 CLONEDUP_APPLY_BLOCKS_PER_TXG 1
log_must set_tunable32 TXG_TIMEOUT 5000
log_must zpool clonedup $TESTPOOL
typeset -i i
for ((i = 0; i < 600; i++)); do
	(( $(clonedup_kstat punched) >= 1 )) && break
	sleep 0.1
done
sleep 2
log_must [ $(clonedup_kstat punched) -eq 1 ]
log_must zfs snapshot $TESTPOOL/a@snap
log_must set_tunable32 TXG_TIMEOUT $txg_timeout
clonedup_sync
log_must zpool wait -t clonedup $TESTPOOL

clonedup_kstat_is punched 1
clonedup_jstat_is clonedup_applied 1
clonedup_jstat_is clonedup_skipped_stale 1
clonedup_jstat_is errors 0
log_must [ $(zw_data_blocks $TESTPOOL/a s) -eq 1 ]
log_must [ $(zw_data_blocks $TESTPOOL/a d) -eq 0 ]
clonedup_leakcheck

log_pass "a source punch that a snapshot holds counts as a skip"
