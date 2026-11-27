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
#	Ensure resilver copies every DVA added to a fast dedup entry.
#
# STRATEGY:
#	1. Write a deduplicated block with one copy and offline a mirror child.
#	2. Write the same block with copies=2, then online the child.
#	3. Verify resilver completes and a scrub has nothing to repair.
#

. $STF_SUITE/include/libtest.shlib

verify_runnable "global"

TPOOL=dedupbirthresilver
TESTDIR=$TEST_BASE_DIR/dedup-birth-resilver
VDEVDIR=$TESTDIR/vdev
MOUNTPOINT=$TESTDIR/mnt

function cleanup
{
	if poolexists $TPOOL; then
		destroy_pool $TPOOL
	fi
	rm -rf $TESTDIR
	log_must restore_tunable DEDUP_LOG_TXG_MAX
	log_must restore_tunable DEDUP_LOG_FLUSH_ENTRIES_MIN
}

log_assert "Fast dedup resilver writes copies added in a later txg"
log_must save_tunable DEDUP_LOG_TXG_MAX
log_must save_tunable DEDUP_LOG_FLUSH_ENTRIES_MIN
log_onexit cleanup
log_must set_tunable32 DEDUP_LOG_TXG_MAX 1
log_must set_tunable32 DEDUP_LOG_FLUSH_ENTRIES_MIN 100000

log_must mkdir -p $VDEVDIR
log_must truncate -s 512M $VDEVDIR/vdev1 $VDEVDIR/vdev2
log_must zpool create -f -o feature@fast_dedup=enabled \
	-o feature@block_cloning=disabled -o cachefile=none \
	-O dedup=on -O compression=off -O recordsize=128k \
	-O mountpoint=$MOUNTPOINT $TPOOL mirror \
	$VDEVDIR/vdev1 $VDEVDIR/vdev2
log_must zfs create -o copies=1 $TPOOL/a
log_must zfs create -o copies=2 $TPOOL/b

mount_a=$(get_prop mountpoint $TPOOL/a)
mount_b=$(get_prop mountpoint $TPOOL/b)
log_must dd if=/dev/urandom of=$mount_a/file bs=128k count=256
sync_pool $TPOOL

log_must zpool offline $TPOOL $VDEVDIR/vdev2
log_must dd if=$mount_a/file of=$mount_b/file bs=128k count=256
sync_pool $TPOOL
log_must zpool online $TPOOL $VDEVDIR/vdev2
log_must zpool wait -t resilver $TPOOL

log_must zpool scrub -w $TPOOL
log_must check_pool_status $TPOOL scan "repaired 0B"
log_must cmp $mount_a/file $mount_b/file

log_pass "Fast dedup resilver writes copies added in a later txg"
