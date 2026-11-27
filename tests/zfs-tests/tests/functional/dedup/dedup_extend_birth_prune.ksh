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
#	Ensure pruning an old fast dedup entry does not leak a later copy.
#
# STRATEGY:
#	1. Write a unique block with copies=1, then request copies=2 for it.
#	2. Drop the second reference and prune the unique DDT entry.
#	3. Verify zdb reports no leaked space.
#

. $STF_SUITE/include/libtest.shlib

verify_runnable "global"

TPOOL=dedupbirthprune
TESTDIR=$TEST_BASE_DIR/dedup-birth-prune
VDEVDIR=$TESTDIR/vdev
MOUNTPOINT=$TESTDIR/mnt
SEED=$TESTDIR/seed

function cleanup
{
	if poolexists $TPOOL; then
		destroy_pool $TPOOL
	fi
	rm -rf $TESTDIR
	log_must restore_tunable DEDUP_LOG_TXG_MAX
	log_must restore_tunable DEDUP_LOG_FLUSH_ENTRIES_MIN
}

log_assert "Fast dedup pruning frees every DVA in an entry"
log_must save_tunable DEDUP_LOG_TXG_MAX
log_must save_tunable DEDUP_LOG_FLUSH_ENTRIES_MIN
log_onexit cleanup
log_must set_tunable32 DEDUP_LOG_TXG_MAX 1
log_must set_tunable32 DEDUP_LOG_FLUSH_ENTRIES_MIN 100000

log_must mkdir -p $VDEVDIR
log_must truncate -s 512M $VDEVDIR/vdev1
log_must zpool create -f -o feature@fast_dedup=enabled \
	-o feature@block_cloning=disabled -o cachefile=none \
	-O dedup=on -O compression=off -O copies=1 -O recordsize=128k \
	-O mountpoint=$MOUNTPOINT $TPOOL $VDEVDIR/vdev1

log_must dd if=/dev/urandom of=$SEED bs=128k count=1
log_must cp $SEED $MOUNTPOINT/a
sync_pool $TPOOL
log_must zfs set copies=2 $TPOOL
log_must cp $SEED $MOUNTPOINT/b
sync_pool $TPOOL

log_must rm -f $MOUNTPOINT/b
sync_pool $TPOOL
sleep 1
log_must zpool ddtprune -p 100 $TPOOL
sync_pool $TPOOL

zdb_out=$(zdb -bcc $TPOOL 2>&1)
zdb_rc=$?
if (( zdb_rc != 0 )); then
	log_fail "zdb -bcc failed after pruning: $zdb_out"
fi
if ! print -r -- "$zdb_out" | grep -q "No leaks"; then
	log_fail "zdb found leaked space after pruning"
fi

log_pass "Fast dedup pruning frees every DVA in an entry"
