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
# Copyright (c) 2026, John White.
#

. $STF_SUITE/tests/functional/async_mirror/async_mirror.kshlib

#
# DESCRIPTION:
#	A frozen pool does not give the lagging child a txg that the leader
#	lacks.  After "zpool freeze" the txgs still sync, but they write no
#	uberblock and their frees are dropped.  If the replay copied such a
#	txg and wrote its uberblock to the lagging child, the next import
#	selected that uberblock, and the blocks freed in the frozen txgs
#	stayed allocated with nothing pointing to them: zdb found a leak.
#
# STRATEGY:
#	1. Create a pool with a two-way mirror and turn the async mirror on.  Write
#	   file a and wait until the lagging child has caught up.
#	2. Write a small file with sync writes, so that the pool has a ZIL
#	   header, and freeze the pool.  Remove file a, and write file b
#	   with sync writes: the commit for b puts both in the intent log.
#	   Sync a few txgs.
#	3. Wait until the replay handles the pending txgs.  Verify that the
#	   newest uberblock of the lagging child is not newer than the newest
#	   uberblock of the leader.
#	4. Export, and import, which claims and replays the intent log.
#	   Verify that b is there.  Wait until the lagging child has caught
#	   up, and export.
#	5. Verify that zdb finds no leaked space.
#

verify_runnable "global"

log_onexit cleanup_async_mirror

async_mirror_create_pool
async_mirror_enable
async_mirror_write_files a 1
log_must wait_async_mirror_idle

# After the freeze, ZIL records are created only if a ZIL header already
# exists, as in slog_replay_fs_001: write a file synchronously first.
log_must dd if=/dev/zero of=/$TESTPOOL/sync conv=fdatasync,fsync bs=1 count=1
log_must zpool freeze $TESTPOOL
log_must rm /$TESTPOOL/a.0
log_must dd if=/dev/urandom of=/$TESTPOOL/b oflag=sync bs=128k count=8
typeset -i i
for (( i = 0; i < 4; i++ )); do
	log_must zpool sync -l $TESTPOOL
done
log_must wait_async_mirror_stat pending_txgs -eq 0

typeset -i lead=$(get_ring_max_txg $ASYNC_MIRROR_LEADER)
typeset -i lag=$(get_ring_max_txg $ASYNC_MIRROR_LAGGING)
log_note "newest txg: leader $lead, lagging child $lag"
log_must test $lag -le $lead

log_must zpool export $TESTPOOL
log_must zpool import -f -d $ASYNC_MIRROR_DIR $TESTPOOL
log_must test -f /$TESTPOOL/b
log_mustnot test -e /$TESTPOOL/a.0
sync_pool $TESTPOOL
log_must wait_async_mirror_idle
log_must zpool export $TESTPOOL

log_must eval "zdb -e -p $ASYNC_MIRROR_DIR -bcc $TESTPOOL > /dev/null"

async_mirror_import
log_pass "A frozen pool gives the lagging child no txg that the leader lacks."
