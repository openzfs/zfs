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
#	Verify that a split zvol write that crosses a multiple of
#	SPA_MAXBLOCKSIZE (16M) is replayed whole when all its pieces are
#	in the log, and not at all if the log ends after that offset:
#	replay applies a write in parts only if it is too large to gather.
#
# STRATEGY:
#	1. Create a pool with a slog and a zvol with sync=always, and freeze
#	   the pool.
#	2. Write 768K at 15.75M in one synchronous O_DIRECT write(2).  It is
#	   logged as one record split into about seven pieces, one of which
#	   contains offset 16M.  768K reaches the zvol whole on Linux
#	   (BIO_MAX_VECS pages, or with zvol_use_blk_mq, 8 volume blocks)
#	   and FreeBSD (MAXPHYS).
#	3. Export and import to replay the log.  The zvol must hold the
#	   write, and zil_replay_split_merged must count it.
#	4. Repeat with different data, but overwrite the log block holding
#	   the first piece after 16M with zeros before importing, as if the
#	   system had crashed before it reached disk.  None of the second
#	   write may be replayed.
#

verify_runnable "global"

VOLDEV=$ZVOL_DEVDIR/$TESTPOOL/$TESTVOL
K=1024
M=$((1024 * 1024))

#
# Freeze the pool, write $1 to the zvol at 15.75M in one write(2), leaving
# the write only in the log.
#
function write_frozen
{
	typeset src=$1

	# Make sure the zvol has a ZIL header before freezing.
	log_must dd if=/dev/zero of=$VOLDEV bs=512 seek=$((47 * M / 512)) \
	    count=1 oflag=direct,sync
	log_must zpool freeze $TESTPOOL
	log_must dd if=$src of=$VOLDEV bs=768k seek=21 count=1 \
	    oflag=direct,sync
}

#
# Print the number of the first log block of $1 that holds a TX_WRITE piece
# at or after offset $2.
#
function block_after
{
	typeset ds=$1
	typeset boundary=$2
	typeset line off
	typeset -i n=0

	zdb -ivvvvv $ds | grep -a -E "Block seqno|foid .*, offset " | \
	    while read line; do
		case "$line" in
		*"Block seqno"*)
			n=$((n + 1))
			;;
		*)
			off=${line#*offset }
			off=${off%%,*}
			if (( 16#$off >= boundary )); then
				echo $n
				return
			fi
			;;
		esac
	done
}

#
# Compare $3 bytes of the zvol at offset $2 with file $1, at the same
# offset within it as $4.
#
function zvol_cmp
{
	typeset src=$1
	typeset off=$2
	typeset len=$3
	typeset srcoff=$4

	log_must eval "dd if=$VOLDEV of=$TESTDIR/got bs=64k skip=$((off / \
	    (64 * K))) count=$((len / (64 * K)))"
	log_must eval "dd if=$src of=$TESTDIR/want bs=64k skip=$((srcoff / \
	    (64 * K))) count=$((len / (64 * K)))"
	log_must cmp $TESTDIR/want $TESTDIR/got
}

log_assert "Replay applies a split zvol write that crosses a multiple of" \
    "16M only if all of it is in the log."
log_onexit cleanup
log_must setup

log_must zpool create $TESTPOOL $VDEV log mirror $LDEV
log_must zfs create -V 48m -o volblocksize=128k -o compression=off \
    -o sync=always $TESTPOOL/$TESTVOL
block_device_wait $VOLDEV
log_must mkdir -p $TESTDIR

#
# A write crossing 16M is replayed whole.
#
log_must dd if=/dev/urandom of=$TESTDIR/src1 bs=768k count=1
merged=$(kstat zil.zil_replay_split_merged)
write_frozen $TESTDIR/src1
log_must_busy zpool export $TESTPOOL
log_must zpool import -f -d $VDIR $TESTPOOL
block_device_wait $VOLDEV
zvol_cmp $TESTDIR/src1 $((63 * 256 * K)) $((768 * K)) 0
merged=$(( $(kstat zil.zil_replay_split_merged) - merged ))
log_note "zil_replay_split_merged went up by $merged"
log_must test $merged -eq 1

#
# A write crossing 16M whose pieces after 16M are lost is not replayed,
# not even up to 16M.
#
log_must dd if=/dev/urandom of=$TESTDIR/src2 bs=768k count=1
merged=$(kstat zil.zil_replay_split_merged)
dropped=$(kstat zil.zil_replay_split_dropped)
write_frozen $TESTDIR/src2
n=$(block_after $TESTPOOL/$TESTVOL $((16 * M)))
[[ -n $n ]] || log_fail "no log block after 16M"
lose_zil_block $TESTPOOL/$TESTVOL $n
log_must zpool import -f -d $VDIR $TESTPOOL
block_device_wait $VOLDEV
zvol_cmp $TESTDIR/src1 $((63 * 256 * K)) $((768 * K)) 0
merged=$(( $(kstat zil.zil_replay_split_merged) - merged ))
dropped=$(( $(kstat zil.zil_replay_split_dropped) - dropped ))
log_note "zil_replay_split_merged went up by $merged," \
    "zil_replay_split_dropped by $dropped"
log_must test $merged -eq 0
log_must test $dropped -ge 1

log_pass "Replay applies a split zvol write that crosses a multiple of" \
    "16M only if all of it is in the log."
