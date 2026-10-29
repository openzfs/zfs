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
#	Releasing the last hold on a snapshot marked for deferred
#	destroy destroys the snapshot, also while the apply holds it
#	as a group's surviving copy.  The release asks the apply to
#	let go instead of failing with EBUSY.
#
# STRATEGY:
#	1. Write a file, snapshot it and remove it, so the only older
#	   copy lives in the snapshot, and copy it twice into another
#	   filesystem.
#	2. Hold the snapshot, then destroy it with -d, which leaves it
#	   to the release.
#	3. Apply slowly, and release the hold while the apply runs.
#	4. Check that the release destroyed the snapshot, and that it
#	   found the apply holding a dataset.
#

verify_runnable "global"

typeset -ri nblk=64

function cleanup
{
	log_must restore_tunable CLONEDUP_APPLY_TXG_DELAY
	log_must restore_tunable CLONEDUP_APPLY_BLOCKS_PER_TXG
	clonedup_cleanup
}

log_assert "zfs release of a deferred destroy works during the apply"
log_onexit cleanup
log_must save_tunable CLONEDUP_APPLY_TXG_DELAY
log_must save_tunable CLONEDUP_APPLY_BLOCKS_PER_TXG

clonedup_pool_create

clonedup_write /$TESTPOOL/a $nblk
clonedup_sync
log_must zfs snapshot $TESTPOOL@snap
log_must rm /$TESTPOOL/a
clonedup_sync

log_must zfs create $TESTPOOL/dd
log_must dd if=/$TESTPOOL/.zfs/snapshot/snap/a of=/$TESTPOOL/dd/b \
    bs=$CD_BS status=none
log_must dd if=/$TESTPOOL/.zfs/snapshot/snap/a of=/$TESTPOOL/dd/c \
    bs=$CD_BS status=none
clonedup_sync

log_must zfs hold keep $TESTPOOL@snap
log_must zfs destroy -d $TESTPOOL@snap
log_must datasetexists $TESTPOOL@snap

log_must set_tunable32 CLONEDUP_APPLY_TXG_DELAY 200
log_must set_tunable32 CLONEDUP_APPLY_BLOCKS_PER_TXG 4

log_must zpool clonedup $TESTPOOL
clonedup_wait_applying
sleep 1
typeset -i yields=$(clonedup_kstat yields)
log_must zfs release keep $TESTPOOL@snap
log_mustnot datasetexists $TESTPOOL@snap
clonedup_kstat_gt yields $yields

log_must set_tunable32 CLONEDUP_APPLY_TXG_DELAY 0
log_must zpool wait -t clonedup $TESTPOOL
clonedup_jstat_is errors 0
clonedup_leakcheck

log_pass "zfs release of a deferred destroy works during the apply"
