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
#	An incremental receive into an unmounted dataset finishes
#	while the apply thread is rewriting that dataset's files.  The
#	dataset has been written since its last snapshot, so the
#	apply rewrites it, and the receive needs -F.
#

verify_runnable "global"

function cleanup
{
	log_must restore_tunable CLONEDUP_APPLY_TXG_DELAY
	log_must restore_tunable CLONEDUP_APPLY_BLOCKS_PER_TXG
	log_must restore_tunable TXG_TIMEOUT
	rm -f $TEST_BASE_DIR/clonedup_keep
	clonedup_cleanup
}

log_assert "receive finishes while the apply holds its destination"
log_onexit cleanup
log_must save_tunable CLONEDUP_APPLY_TXG_DELAY
log_must save_tunable CLONEDUP_APPLY_BLOCKS_PER_TXG
log_must save_tunable TXG_TIMEOUT
clonedup_pool_create
typeset nblk=64

log_must zfs create $TESTPOOL/src
clonedup_write /$TESTPOOL/src/a $nblk
clonedup_dup /$TESTPOOL/src/a /$TESTPOOL/src/b
log_must cp /$TESTPOOL/src/a $TEST_BASE_DIR/clonedup_keep
clonedup_sync
log_must zfs snapshot $TESTPOOL/src@1
log_must eval "zfs send $TESTPOOL/src@1 | zfs recv -u $TESTPOOL/rcv"
log_must zfs mount $TESTPOOL/rcv
clonedup_write /$TESTPOOL/rcv/n 1
log_must zfs unmount $TESTPOOL/rcv

clonedup_write /$TESTPOOL/src/c 4
clonedup_sync
log_must zfs snapshot $TESTPOOL/src@2

log_must set_tunable32 CLONEDUP_APPLY_TXG_DELAY 100
log_must set_tunable32 CLONEDUP_APPLY_BLOCKS_PER_TXG 4
log_must set_tunable32 TXG_TIMEOUT 1

log_must zpool clonedup $TESTPOOL
clonedup_wait_applying
sleep 1
log_must eval \
    "zfs send -i @1 $TESTPOOL/src@2 | zfs recv -F -u $TESTPOOL/rcv"
log_must zpool wait -t clonedup $TESTPOOL

clonedup_jstat_gt clonedup_applied 0
clonedup_jstat_is errors 0
# The operation found the apply holding datasets and made it let
# go, which is the path under test; without that it proved nothing.
clonedup_kstat_gt yields 0
log_must datasetexists $TESTPOOL/rcv@2
log_must zfs mount $TESTPOOL/rcv
log_must cmp /$TESTPOOL/rcv/a $TEST_BASE_DIR/clonedup_keep
log_must cmp /$TESTPOOL/rcv/b $TEST_BASE_DIR/clonedup_keep
log_must cmp /$TESTPOOL/rcv/c /$TESTPOOL/src/c
clonedup_leakcheck

log_pass "receive finishes while the apply holds its destination"
