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
# Copyright 2026 Oxide Computer Company
#

. $STF_SUITE/tests/functional/slog/slog.kshlib

#
# DESCRIPTION:
#	Verify that a zvol write split into many pieces across log blocks
#	is replayed whole, and that one whose later pieces are lost is not
#	replayed at all.
#
# STRATEGY:
#	1. Create a pool with a slog and a zvol, and freeze the pool.  The
#	   zvol has sync=always, since FreeBSD's GEOM zvols ignore O_SYNC.
#	2. Write 512K to the zvol in one synchronous O_DIRECT write(2).  It
#	   is logged as a single record with its data (WR_NEED_COPY), split
#	   into about five pieces: the first marked "more follows", the last
#	   "continued", and the ones between both.  512K is well under the
#	   largest single request on Linux (BIO_MAX_VECS pages, or with
#	   zvol_use_blk_mq, zvol_blk_mq_blocks_per_thread volume blocks) and
#	   FreeBSD (MAXPHYS), so the write reaches the zvol whole.
#	3. Export and import to replay the log.  The zvol must hold the
#	   write, and zil_replay_split_merged must count it.
#	4. Repeat at the next 512K, but overwrite the fourth log block with
#	   zeros before importing, so the log ends after the third, as if
#	   the system had crashed before the fourth reached disk.  None of
#	   that write may be replayed, the pieces in the first three blocks
#	   must be counted in zil_replay_split_dropped, and the first write
#	   must still be intact.
#

verify_runnable "global"

VOLDEV=$ZVOL_DEVDIR/$TESTPOOL/$TESTVOL

#
# Freeze the pool, write $1 to the zvol at 512K offset $2 in one write(2),
# leaving the write only in the log.
#
function write_frozen
{
	typeset src=$1
	typeset seek=$2

	# Make sure the zvol has a ZIL header before freezing.
	log_must dd if=/dev/zero of=$VOLDEV bs=512 seek=8192 count=1 \
	    oflag=direct,sync
	log_must zpool freeze $TESTPOOL
	log_must dd if=$src of=$VOLDEV bs=512k seek=$seek count=1 \
	    oflag=direct,sync
}

#
# Export and import to replay the log.  If $1 is "lose", make the log end
# after its third block.
#
function replay_log
{
	if [[ "$1" == "lose" ]]; then
		lose_zil_block $TESTPOOL/$TESTVOL 4
	else
		log_must_busy zpool export $TESTPOOL
	fi
	log_must zpool import -f -d $VDIR $TESTPOOL
	block_device_wait $VOLDEV
}

log_assert "Replay applies a zvol write split into many pieces whole or" \
    "not at all."
log_onexit cleanup
log_must setup

log_must zpool create $TESTPOOL $VDEV log mirror $LDEV
log_must zfs create -V 16m -o volblocksize=128k -o compression=off \
    -o sync=always $TESTPOOL/$TESTVOL
block_device_wait $VOLDEV
log_must mkdir -p $TESTDIR

#
# A write split into many pieces is replayed whole.
#
log_must dd if=/dev/urandom of=$TESTDIR/src1 bs=512k count=1
merged=$(kstat zil.zil_replay_split_merged)
write_frozen $TESTDIR/src1 0

log_note "Verify the split pieces in the log:"
log_must eval "zdb -ivvvvv $TESTPOOL/$TESTVOL >$TESTDIR/zdb.out"
log_must grep -q "continued, more follows" $TESTDIR/zdb.out
log_must test $(grep -c "more follows" $TESTDIR/zdb.out) -ge 3
log_must test $(grep -c "continued" $TESTDIR/zdb.out) -ge 3

replay_log
log_must eval "dd if=$VOLDEV of=$TESTDIR/got1 bs=512k count=1"
log_must cmp $TESTDIR/src1 $TESTDIR/got1
merged=$(( $(kstat zil.zil_replay_split_merged) - merged ))
log_note "zil_replay_split_merged went up by $merged"
log_must test $merged -eq 1

#
# A write whose later pieces are lost is not replayed.
#
log_must dd if=/dev/urandom of=$TESTDIR/src2 bs=512k count=1
log_must dd if=/dev/zero of=$TESTDIR/zero bs=512k count=1
merged=$(kstat zil.zil_replay_split_merged)
dropped=$(kstat zil.zil_replay_split_dropped)
write_frozen $TESTDIR/src2 1
replay_log lose
log_must eval "dd if=$VOLDEV of=$TESTDIR/got2 bs=512k skip=1 count=1"
log_must cmp $TESTDIR/zero $TESTDIR/got2
log_must eval "dd if=$VOLDEV of=$TESTDIR/got1 bs=512k count=1"
log_must cmp $TESTDIR/src1 $TESTDIR/got1
merged=$(( $(kstat zil.zil_replay_split_merged) - merged ))
dropped=$(( $(kstat zil.zil_replay_split_dropped) - dropped ))
log_note "zil_replay_split_merged went up by $merged," \
    "zil_replay_split_dropped by $dropped"
log_must test $merged -eq 0
log_must test $dropped -ge 2

log_pass "Replay applies a zvol write split into many pieces whole or" \
    "not at all."
