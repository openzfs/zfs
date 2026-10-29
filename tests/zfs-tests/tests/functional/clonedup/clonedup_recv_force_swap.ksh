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
#	A forced incremental receive into a mounted filesystem whose
#	files are being rewritten succeeds, and the files end up with
#	the received content.
#

verify_runnable "global"

function cleanup
{
	log_must restore_tunable CLONEDUP_APPLY_TXG_DELAY
	log_must restore_tunable CLONEDUP_APPLY_BLOCKS_PER_TXG
	log_must restore_tunable TXG_TIMEOUT
	clonedup_cleanup
}

log_assert "zfs receive -F succeeds while the apply writes the target"
log_onexit cleanup
log_must save_tunable CLONEDUP_APPLY_TXG_DELAY
log_must save_tunable CLONEDUP_APPLY_BLOCKS_PER_TXG
log_must save_tunable TXG_TIMEOUT

clonedup_pool_create
typeset nblk=64

log_must zfs create $TESTPOOL/src
clonedup_write /$TESTPOOL/src/a $nblk
clonedup_dup /$TESTPOOL/src/a /$TESTPOOL/src/b
clonedup_sync
log_must zfs snapshot $TESTPOOL/src@1
log_must eval "zfs send $TESTPOOL/src@1 | zfs recv $TESTPOOL/rcv"
log_must ismounted $TESTPOOL/rcv
# written since rcv@1, so the apply rewrites rcv
clonedup_write /$TESTPOOL/rcv/n 1

clonedup_write /$TESTPOOL/src/a $nblk
clonedup_sync
log_must zfs snapshot $TESTPOOL/src@2

log_must set_tunable32 CLONEDUP_APPLY_TXG_DELAY 100
log_must set_tunable32 CLONEDUP_APPLY_BLOCKS_PER_TXG 4
log_must set_tunable32 TXG_TIMEOUT 1

log_must zpool clonedup $TESTPOOL
clonedup_wait_applying
sleep 1
log_must eval \
    "zfs send -i @1 $TESTPOOL/src@2 | zfs recv -F $TESTPOOL/rcv"
log_must zpool wait -t clonedup $TESTPOOL

clonedup_jstat_gt clonedup_applied 0
clonedup_jstat_is errors 0
# The operation found the apply holding datasets and made it let
# go, which is the path under test; without that it proved nothing.
clonedup_kstat_gt yields 0
log_must datasetexists $TESTPOOL/rcv@2
log_must cmp /$TESTPOOL/rcv/a /$TESTPOOL/src/a
log_must cmp /$TESTPOOL/rcv/b /$TESTPOOL/src/b
clonedup_leakcheck

log_pass "zfs receive -F succeeds while the apply writes the target"
