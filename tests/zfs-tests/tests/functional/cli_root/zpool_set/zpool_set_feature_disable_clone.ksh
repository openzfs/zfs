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
# Copyright (c) 2026 Tigerblue77
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#	A TX_CLONE_RANGE record needs block_cloning when it is replayed.
#	Claiming the log at import adds BRT entries only for the cloned
#	blocks that are neither holes nor embedded, so a record of holes
#	and embedded blocks leaves the feature enabled but not active.
#	block_cloning must not be disabled while such a log waits to be
#	replayed.
#
# STRATEGY:
#	1. Create a pool on which nothing was cloned, with a sparse file and
#	   a file small enough to be embedded in its block pointer, and an
#	   intent log on disk.
#	2. Freeze the pool, clone both files and unmount the file system:
#	   its intent log holds TX_CLONE_RANGE records.
#	3. Export the pool and import it without mounting: block_cloning is
#	   enabled, not active, and disabling it must fail.
#	4. Mount the file system, which replays the log: the clones must
#	   match their sources, and block_cloning can then be disabled.
#

verify_runnable "global"

if ! command -v clonefile > /dev/null ; then
	log_unsupported "clonefile program required to test block cloning"
fi

VDIR=$TEST_BASE_DIR/feature_clone
FS=$TESTPOOL1/fs

function cleanup
{
	destroy_pool $TESTPOOL1
	rm -rf $VDIR
	if tunable_exists BCLONE_ENABLED ; then
		log_must restore_tunable BCLONE_ENABLED
	fi
}

function check_prop # feature value
{
	typeset val=$(get_pool_prop feature@$1 $TESTPOOL1)

	[[ "$val" == "$2" ]] ||
	    log_fail "feature@$1 is '$val', expected '$2'"
}

log_assert "block_cloning cannot be disabled while a log with" \
    "TX_CLONE_RANGE records waits to be replayed"
log_onexit cleanup

if tunable_exists BCLONE_ENABLED ; then
	log_must save_tunable BCLONE_ENABLED
	log_must set_tunable32 BCLONE_ENABLED 1
fi

# 1. Holes and an embedded block, nothing cloned yet.
log_must mkdir -p $VDIR
log_must truncate -s $MINVDEVSIZE $VDIR/vdev
log_must zpool create -f $TESTPOOL1 $VDIR/vdev
log_must zfs create -o compression=lz4 $FS
mntpnt=$(get_prop mountpoint $FS)
log_must truncate -s 1m $mntpnt/sparse
log_must eval "echo hello > $mntpnt/small"
log_must dd if=/dev/zero of=$mntpnt/sync bs=512 count=1 oflag=sync
sync_pool $TESTPOOL1
check_prop block_cloning enabled

# 2. TX_CLONE_RANGE records in the log.
log_must zpool freeze $TESTPOOL1
log_must clonefile -f $mntpnt/sparse $mntpnt/sparse.clone
log_must clonefile -f $mntpnt/small $mntpnt/small.clone
log_must zfs unmount $FS
log_must eval "zdb -iv $FS | grep -q TX_CLONE_RANGE"
log_must zpool export $TESTPOOL1

# 3. Claiming the log activated nothing.
log_must zpool import -f -N -d $VDIR $TESTPOOL1
check_prop block_cloning enabled
log_mustnot zpool set feature@block_cloning=disabled $TESTPOOL1
check_prop block_cloning enabled

# 4. Replay, then disable.
log_must zfs mount $FS
log_must cmp $mntpnt/sparse $mntpnt/sparse.clone
log_must cmp $mntpnt/small $mntpnt/small.clone
log_must zpool set feature@block_cloning=disabled $TESTPOOL1
check_prop block_cloning disabled

log_pass "block_cloning cannot be disabled while a log with" \
    "TX_CLONE_RANGE records waits to be replayed"
