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
#	Promoting a clone and rolling back a filesystem succeed while
#	the apply thread is working on their blocks.
#

verify_runnable "global"

function cleanup
{
	log_must restore_tunable CLONEDUP_APPLY_TXG_DELAY
	log_must restore_tunable CLONEDUP_APPLY_BLOCKS_PER_TXG
	log_must restore_tunable TXG_TIMEOUT
	clonedup_cleanup
}

log_assert "promote and rollback succeed while the apply runs"
log_onexit cleanup
log_must save_tunable CLONEDUP_APPLY_TXG_DELAY
log_must save_tunable CLONEDUP_APPLY_BLOCKS_PER_TXG
log_must save_tunable TXG_TIMEOUT

clonedup_pool_create
typeset nblk=32

# a clone whose new files duplicate the origin snapshot's blocks
log_must zfs create $TESTPOOL/p
clonedup_write /$TESTPOOL/p/a $nblk
clonedup_sync
log_must zfs snapshot $TESTPOOL/p@origin
log_must zfs clone $TESTPOOL/p@origin $TESTPOOL/pc
clonedup_dup /$TESTPOOL/pc/a /$TESTPOOL/pc/b
clonedup_dup /$TESTPOOL/pc/a /$TESTPOOL/pc/c

# a filesystem with a snapshot to roll back to and duplicates after it
log_must zfs create $TESTPOOL/r
clonedup_write /$TESTPOOL/r/a $nblk
clonedup_sync
log_must zfs snapshot $TESTPOOL/r@back
clonedup_dup /$TESTPOOL/r/a /$TESTPOOL/r/b
clonedup_dup /$TESTPOOL/r/a /$TESTPOOL/r/c
clonedup_sync

log_must set_tunable32 CLONEDUP_APPLY_TXG_DELAY 100
log_must set_tunable32 CLONEDUP_APPLY_BLOCKS_PER_TXG 4
log_must set_tunable32 TXG_TIMEOUT 1

log_must zpool clonedup $TESTPOOL
clonedup_wait_applying
sleep 1
log_must zfs promote $TESTPOOL/pc
log_must zfs rollback $TESTPOOL/r@back
log_must zpool wait -t clonedup $TESTPOOL

clonedup_jstat_gt clonedup_applied 0
clonedup_jstat_is errors 0
# The operation found the apply holding datasets and made it let
# go, which is the path under test; without that it proved nothing.
clonedup_kstat_gt yields 0
log_must datasetexists $TESTPOOL/pc@origin
log_mustnot [ -e /$TESTPOOL/r/b ]
log_must cmp /$TESTPOOL/pc/a /$TESTPOOL/pc/b
log_must cmp /$TESTPOOL/pc/a /$TESTPOOL/pc/c
clonedup_leakcheck

log_pass "promote and rollback succeed while the apply runs"
