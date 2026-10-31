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

. $STF_SUITE/tests/functional/rangelock/rangelock.kshlib

#
# DESCRIPTION:
#	A read of a range that block cloning replaces returns the
#	data from before the clone or from after it.
#
# STRATEGY:
#	1. In a file system with recordsize=128k, write a file of
#	   24 regions of 128K.  The 8 in the middle are read and
#	   replaced, the 8 below hold one generation and the 8
#	   above another.  Write two more files laid out the same
#	   way, each holding one of the two generations.  Sync the
#	   pool.
#	2. One thread clones regions of the other two files into
#	   the middle regions, and one clones the regions below and
#	   above them in the same file, alternating generations.
#	   Four threads read the middle regions.  Linux clones with
#	   FICLONERANGE, FreeBSD with copy_file_range().
#	3. Each read must hold one generation per region, and one
#	   that was current while the read ran.
#	4. Fail on any other read, if no read overlapped a clone of
#	   its range, or if the pool has no cloned blocks.
#

verify_runnable "global"

FS=$TESTPOOL/$TESTFS/clone

function cleanup
{
	datasetexists $FS && destroy_dataset $FS -r
	if tunable_exists BCLONE_ENABLED; then
		log_must restore_tunable BCLONE_ENABLED
	fi
}

log_assert "Reads racing clones of the same range are not torn"
log_onexit cleanup

if tunable_exists BCLONE_ENABLED; then
	log_must save_tunable BCLONE_ENABLED
	log_must set_tunable32 BCLONE_ENABLED 1
fi

log_must zfs create -o recordsize=128k $FS
MNT=$(get_prop mountpoint $FS)
log_must rangelock_race -p clone $MNT/file
sync_pool $TESTPOOL
rl_run 60 120 clone $MNT/file
sync_pool $TESTPOOL
used=$(get_pool_prop bcloneused $TESTPOOL)
log_note "bcloneused $used"
log_must test $used -gt 0

log_pass "Reads racing clones of the same range are not torn"
