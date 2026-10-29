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
#	A group of equal blocks larger than the apply thread takes at
#	once is finished across several takes, keeps one surviving
#	copy through a pause and resume, and the run only completes
#	once every copy is done.
#

verify_runnable "global"

function cleanup
{
	log_must restore_tunable CLONEDUP_APPLY_BLOCKS_PER_TXG
	log_must restore_tunable TXG_TIMEOUT
	log_must restore_tunable CLONEDUP_APPLY_TXG_DELAY
	clonedup_cleanup
}

log_assert "large groups are completed across takes and a pause"
log_onexit cleanup
log_must save_tunable CLONEDUP_APPLY_BLOCKS_PER_TXG
log_must save_tunable TXG_TIMEOUT
log_must save_tunable CLONEDUP_APPLY_TXG_DELAY

clonedup_pool_create
typeset ncopies=64

clonedup_write /$TESTPOOL/seed 1
for ((i = 0; i < ncopies; i++)); do
	log_must dd if=/$TESTPOOL/seed of=/$TESTPOOL/c$i bs=$CD_BS \
	    status=none
done
clonedup_sync

log_must set_tunable32 CLONEDUP_APPLY_BLOCKS_PER_TXG 16
log_must set_tunable32 TXG_TIMEOUT 1
log_must set_tunable32 CLONEDUP_APPLY_TXG_DELAY 200

log_must zpool clonedup $TESTPOOL
clonedup_wait_applying
sleep 2
log_must zpool clonedup -p $TESTPOOL
sleep 2
log_must clonedup_is_paused
log_must zpool clonedup $TESTPOOL
log_must zpool wait -t clonedup $TESTPOOL

clonedup_jstat_is clonedup_applied $ncopies
clonedup_jstat_is errors 0
clonedup_jstat_is clonedup_skipped_stale 0
typeset saved=$(zpool get -Hpo value bclonesaved $TESTPOOL)
typeset used=$(zpool get -Hpo value bcloneused $TESTPOOL)
log_must [ $saved -eq $((ncopies * used)) ]
clonedup_check_shared $TESTPOOL seed $TESTPOOL c0 "0"
clonedup_check_shared $TESTPOOL seed $TESTPOOL c31 "0"
clonedup_check_shared $TESTPOOL seed $TESTPOOL c$((ncopies - 1)) "0"
log_must cmp /$TESTPOOL/seed /$TESTPOOL/c$((ncopies - 1))
clonedup_leakcheck

log_pass "large groups are completed across takes and a pause"
