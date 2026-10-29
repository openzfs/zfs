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
#	Destroying the dataset being written to, and the snapshot
#	serving as the surviving copy, succeeds while the apply runs.
#

verify_runnable "global"

function cleanup
{
	log_must restore_tunable CLONEDUP_APPLY_TXG_DELAY
	log_must restore_tunable CLONEDUP_APPLY_BLOCKS_PER_TXG
	log_must restore_tunable TXG_TIMEOUT
	clonedup_cleanup
}

log_assert "destroy succeeds while the apply holds the dataset"
log_onexit cleanup
log_must save_tunable CLONEDUP_APPLY_TXG_DELAY
log_must save_tunable CLONEDUP_APPLY_BLOCKS_PER_TXG
log_must save_tunable TXG_TIMEOUT

clonedup_pool_create
typeset nblk=64

# the surviving copy lives only in a snapshot
clonedup_write /$TESTPOOL/a $nblk
clonedup_sync
log_must zfs snapshot $TESTPOOL@snap
log_must rm /$TESTPOOL/a
clonedup_sync

# destinations in an unmounted filesystem, owned by the apply thread
log_must zfs create $TESTPOOL/dd
log_must dd if=/$TESTPOOL/.zfs/snapshot/snap/a of=/$TESTPOOL/dd/b \
    bs=$CD_BS status=none
log_must dd if=/$TESTPOOL/.zfs/snapshot/snap/a of=/$TESTPOOL/dd/c \
    bs=$CD_BS status=none
clonedup_sync

log_must zfs create $TESTPOOL/keep
clonedup_write /$TESTPOOL/keep/x $nblk
clonedup_dup /$TESTPOOL/keep/x /$TESTPOOL/keep/y
clonedup_sync

log_must zfs unmount $TESTPOOL/dd

log_must set_tunable32 CLONEDUP_APPLY_TXG_DELAY 100
log_must set_tunable32 CLONEDUP_APPLY_BLOCKS_PER_TXG 4
log_must set_tunable32 TXG_TIMEOUT 1

log_must zpool clonedup $TESTPOOL
clonedup_wait_applying
sleep 1
log_must zfs destroy $TESTPOOL/dd
log_must zfs destroy $TESTPOOL@snap
log_must zpool wait -t clonedup $TESTPOOL

clonedup_jstat_gt clonedup_applied 0
clonedup_jstat_is errors 0
# The operation found the apply holding datasets and made it let
# go, which is the path under test; without that it proved nothing.
clonedup_kstat_gt yields 0
log_mustnot datasetexists $TESTPOOL/dd
log_mustnot datasetexists $TESTPOOL@snap
clonedup_leakcheck

log_pass "destroy succeeds while the apply holds the dataset"
