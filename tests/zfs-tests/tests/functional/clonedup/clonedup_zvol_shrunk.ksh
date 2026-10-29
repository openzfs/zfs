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
#	A volume that zfs set volsize shrinks while the apply holds
#	some of its blocks in a batch gets no clone past its new end.
#	volsize takes zv_suspend_lock as a reader, as the apply does,
#	and not the range lock the apply holds, so the apply reads the
#	size again in the batch's transaction.  Grown back, the volume
#	reads zeros where the shrink freed it.
#
# STRATEGY:
#	1. Fill a volume from a file and keep the volume open, so the
#	   apply takes the open volume's path.
#	2. With one worker that waits before it queues each block,
#	   start a run and let it queue two blocks of the volume.
#	3. Shrink the volume to one block while those blocks wait in
#	   the batch, and let the run finish.
#	4. Check that the volume holds no block past its new end, and
#	   that it reads zeros there once grown back.
#

verify_runnable "global"

typeset tail=$TEST_BASE_DIR/clonedup_shrunk_tail
typeset zero=$TEST_BASE_DIR/clonedup_shrunk_zero

function cleanup
{
	exec 3<&-
	log_must restore_tunable CLONEDUP_APPLY_TXG_DELAY
	log_must restore_tunable CLONEDUP_APPLY_ENABLED
	log_must restore_tunable CLONEDUP_APPLY_BLOCKS_PER_TXG
	log_must restore_tunable CLONEDUP_APPLY_THREADS
	log_must restore_tunable TXG_TIMEOUT
	rm -f $tail $zero
	clonedup_cleanup
}

log_assert "a volume shrunk during a batch gets no block past its end"
log_onexit cleanup
log_must save_tunable CLONEDUP_APPLY_TXG_DELAY
log_must save_tunable CLONEDUP_APPLY_ENABLED
log_must save_tunable CLONEDUP_APPLY_BLOCKS_PER_TXG
log_must save_tunable CLONEDUP_APPLY_THREADS
log_must save_tunable TXG_TIMEOUT

clonedup_pool_create
typeset -i nblk=16
typeset -i nbytes=$((CD_BS * nblk))

clonedup_write /$TESTPOOL/a $nblk
clonedup_sync
log_must zfs create -V $nbytes -b $CD_BS $TESTPOOL/v
block_device_wait $ZVOL_DEVDIR/$TESTPOOL/v
clonedup_zvol_fill /$TESTPOOL/a $TESTPOOL/v
clonedup_sync

# One worker, a pause before each block it queues, and room for every
# block in one batch, which it sends once it has taken them all.
log_must set_tunable32 CLONEDUP_APPLY_THREADS 1
log_must set_tunable32 CLONEDUP_APPLY_TXG_DELAY 500
log_must set_tunable32 CLONEDUP_APPLY_BLOCKS_PER_TXG $((2 * nblk))
log_must set_tunable32 TXG_TIMEOUT 1

exec 3<> $ZVOL_DEVDIR/$TESTPOOL/v

log_must set_tunable32 CLONEDUP_APPLY_ENABLED 0
log_must zpool clonedup $TESTPOOL
clonedup_wait_applying
log_must set_tunable32 CLONEDUP_APPLY_ENABLED 1
# The worker has queued the first two blocks once it takes the third.
clonedup_wait_apply_done 3
log_must zfs set volsize=$CD_BS $TESTPOOL/v
log_must zpool wait -t clonedup $TESTPOOL
exec 3<&-
clonedup_sync

clonedup_jstat_is errors 0
clonedup_jstat_is state FINISHED
clonedup_kstat_gt dst_zvol 0
clonedup_jstat_gt clonedup_applied 0
clonedup_jstat_gt clonedup_skipped_stale 0
log_must [ $(zfs get -Hpo value volsize $TESTPOOL/v) -eq $CD_BS ]
# zdb lists each hole the shrink left as an L0 block with no
# address, because the hole keeps its birth txg.
typeset held=$(echo $(clonedup_vol_blocks $TESTPOOL/v |
    awk '$2 ~ /:/ { print $1 }'))
log_note "v holds data in block(s) '$held' (want '0')"
log_must [ "$held" = "0" ]

# Grown back, the range the shrink freed reads zeros.  A fresh import
# drops every cached copy of the volume.
log_must zfs set volsize=$nbytes $TESTPOOL/v
log_must_busy zpool export $TESTPOOL
log_must zpool import $TESTPOOL
block_device_wait $ZVOL_DEVDIR/$TESTPOOL/v
log_must dd if=$ZVOL_DEVDIR/$TESTPOOL/v of=$tail bs=$CD_BS skip=1 \
    count=$((nblk - 1)) status=none
log_must dd if=/dev/zero of=$zero bs=$CD_BS count=$((nblk - 1)) \
    status=none
log_must cmp $tail $zero
clonedup_leakcheck

log_pass "a volume shrunk during a batch gets no block past its end"
