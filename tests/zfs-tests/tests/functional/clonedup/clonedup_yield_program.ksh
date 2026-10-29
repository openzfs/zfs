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
#	A channel program destroys a snapshot that the apply holds as
#	a group's surviving copy.  The program runs in syncing
#	context and cannot wait there, so the ioctl asks the apply to
#	let go before it starts, instead of the destroy failing with
#	EBUSY.
#
# STRATEGY:
#	1. Write a file and copy it twice into another filesystem,
#	   then snapshot the file and remove it, so its blocks live
#	   only in the snapshot.  The copies come first because a read
#	   through .zfs/snapshot mounts the snapshot, and a channel
#	   program cannot destroy a mounted snapshot.
#	2. Apply slowly, and destroy the snapshot with zfs program
#	   while the apply runs.
#	3. Check that the program succeeded, the snapshot is gone, and
#	   the program found the apply holding a dataset.
#

verify_runnable "global"

typeset -ri nblk=64
typeset -r script=$TEST_BASE_DIR/clonedup_yield_program.zcp

function cleanup
{
	log_must restore_tunable CLONEDUP_APPLY_TXG_DELAY
	log_must restore_tunable CLONEDUP_APPLY_BLOCKS_PER_TXG
	rm -f $script
	clonedup_cleanup
}

log_assert "a channel program destroys a snapshot the apply holds"
log_onexit cleanup
log_must save_tunable CLONEDUP_APPLY_TXG_DELAY
log_must save_tunable CLONEDUP_APPLY_BLOCKS_PER_TXG

cat > $script <<-EOF
	args = ...
	err = zfs.sync.destroy(args["argv"][1])
	if err ~= 0 then
		error("destroy returned " .. err)
	end
	return err
EOF

clonedup_pool_create

clonedup_write /$TESTPOOL/a $nblk
log_must zfs create $TESTPOOL/dd
clonedup_dup /$TESTPOOL/a /$TESTPOOL/dd/b
clonedup_dup /$TESTPOOL/a /$TESTPOOL/dd/c
clonedup_sync
log_must zfs snapshot $TESTPOOL@snap
log_must rm /$TESTPOOL/a
clonedup_sync

log_must set_tunable32 CLONEDUP_APPLY_TXG_DELAY 200
log_must set_tunable32 CLONEDUP_APPLY_BLOCKS_PER_TXG 4

log_must zpool clonedup $TESTPOOL
clonedup_wait_applying
sleep 1
typeset -i yields=$(clonedup_kstat yields)
log_must zfs program $TESTPOOL $script $TESTPOOL@snap
log_mustnot datasetexists $TESTPOOL@snap
clonedup_kstat_gt yields $yields

log_must set_tunable32 CLONEDUP_APPLY_TXG_DELAY 0
log_must zpool wait -t clonedup $TESTPOOL
clonedup_jstat_is errors 0
clonedup_leakcheck

log_pass "a channel program destroys a snapshot the apply holds"
