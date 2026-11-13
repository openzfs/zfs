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
# Copyright (c) 2026 OpenZFS contributors.
#

#
# DESCRIPTION:
#	Ensure fast dedup copies added after a checkpoint are freed safely.
#
# STRATEGY:
#	1. Write a dedup entry with one copy and checkpoint the pool.
#	2. Add a second copy, then free both references.
#	3. Verify zdb can walk the checkpointed space maps after export.
#

. $STF_SUITE/include/libtest.shlib

verify_runnable "global"

TPOOL=dedupbirthckpt
TESTDIR=$TEST_BASE_DIR/dedup-birth-checkpoint
VDEVDIR=$TESTDIR/vdev
MOUNTPOINT=$TESTDIR/mnt

function cleanup
{
	if ! poolexists $TPOOL; then
		zpool import -d $VDEVDIR $TPOOL >/dev/null 2>&1
	fi
	if poolexists $TPOOL; then
		destroy_pool $TPOOL
	fi
	rm -rf $TESTDIR
	log_must restore_tunable DEDUP_LOG_TXG_MAX
	log_must restore_tunable DEDUP_LOG_FLUSH_ENTRIES_MIN
}

log_assert "Fast dedup checkpoint frees use the birth of every copy"
log_must save_tunable DEDUP_LOG_TXG_MAX
log_must save_tunable DEDUP_LOG_FLUSH_ENTRIES_MIN
log_onexit cleanup
log_must set_tunable32 DEDUP_LOG_TXG_MAX 1
log_must set_tunable32 DEDUP_LOG_FLUSH_ENTRIES_MIN 100000

log_must mkdir -p $VDEVDIR
log_must truncate -s 512M $VDEVDIR/vdev1
log_must zpool create -f -o feature@fast_dedup=enabled \
	-o feature@block_cloning=disabled -o cachefile=none \
	-O dedup=on -O compression=off -O recordsize=128k \
	-O mountpoint=$MOUNTPOINT $TPOOL $VDEVDIR/vdev1
log_must zfs create -o copies=1 $TPOOL/a
log_must zfs create -o copies=2 $TPOOL/b

mount_a=$(get_prop mountpoint $TPOOL/a)
mount_b=$(get_prop mountpoint $TPOOL/b)
log_must dd if=/dev/urandom of=$mount_a/file bs=128k count=256
sync_pool $TPOOL
log_must zpool checkpoint $TPOOL

log_must dd if=$mount_a/file of=$mount_b/file bs=128k count=256
sync_pool $TPOOL
log_must rm -f $mount_a/file $mount_b/file
sync_pool $TPOOL
sync_pool $TPOOL
sync_pool $TPOOL

log_must zpool export $TPOOL
log_must zdb -e -p $VDEVDIR $TPOOL
log_must zpool import -d $VDEVDIR $TPOOL
log_must zpool checkpoint -d $TPOOL

log_pass "Fast dedup checkpoint frees use the birth of every copy"
