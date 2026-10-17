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
#	Verify that a write split across log blocks is replayed whole, and
#	that one whose later pieces are lost is not replayed at all.
#
# STRATEGY:
#	1. Create a pool with a slog, so async writes are logged with
#	   their data (WR_NEED_COPY), and freeze it.
#	2. Write 1M in one write(2), and fsync(2) it to write the log:
#	   eight 128K records, each larger than a log block, so each is
#	   split across log blocks.
#	3. Check with zdb that the pieces are marked, then export and
#	   import to replay the log.  The file must match, and
#	   zil_replay_split_merged must count the eight writes.
#	4. Repeat, but overwrite the fourth log block with zeros before
#	   importing, so the log ends after the third, as if the system
#	   had crashed before the fourth reached disk.  Each 128K
#	   of the file must then be either the data written or zeros: the
#	   records wholly in the first three blocks are replayed, and the
#	   one split into the fourth is dropped.
#

verify_runnable "global"

#
# Freeze the pool, write $1 to $2 in one write(2) and fsync it, then replay
# the log.  (Unmounting doesn't write the log on FreeBSD: the file's vnode
# is gone by the time the log is committed, and its data can't be read.)
# If $3 is "check", first check that zdb shows the split pieces; if it is
# "lose", make the log end after its third block.
#
function write_frozen_and_replay
{
	typeset src=$1
	typeset dst=$2
	typeset mode=$3

	# Make sure the dataset has a ZIL header before freezing.
	log_must dd if=/dev/zero of=/$TESTPOOL/$TESTFS/sync \
	    conv=fdatasync,fsync bs=1 count=1
	log_must zpool freeze $TESTPOOL
	log_must dd if=$src of=$dst bs=1024k count=1 conv=fsync
	log_must zfs unmount $TESTPOOL/$TESTFS
	if [[ "$mode" == "check" ]]; then
		typeset zdbout=$TESTDIR/zdb.out
		log_must eval "zdb -ivvvvv $TESTPOOL/$TESTFS >$zdbout"
		log_must test $(grep -c "more follows" $zdbout) -ge 8
		log_must test $(grep -c "continued" $zdbout) -ge 8
	fi
	if [[ "$mode" == "lose" ]]; then
		lose_zil_block $TESTPOOL/$TESTFS 4
	else
		log_must zpool export $TESTPOOL
	fi
	log_must zpool import -f -d $VDIR $TESTPOOL
}

log_assert "Replay applies a write split across log blocks whole or not at all."
log_onexit cleanup
log_must setup

log_must zpool create $TESTPOOL $VDEV log mirror $LDEV
log_must zfs create -o compression=off -o recordsize=128k \
    $TESTPOOL/$TESTFS
log_must mkdir -p $TESTDIR

#
# A split write is replayed whole.
#
log_must dd if=/dev/urandom of=$TESTDIR/src1 bs=1024k count=1
merged=$(kstat zil.zil_replay_split_merged)
write_frozen_and_replay $TESTDIR/src1 /$TESTPOOL/$TESTFS/file1 check
log_must cmp $TESTDIR/src1 /$TESTPOOL/$TESTFS/file1
merged=$(( $(kstat zil.zil_replay_split_merged) - merged ))
log_note "zil_replay_split_merged went up by $merged"
log_must test $merged -ge 8

#
# A split write whose later pieces are lost is not replayed.
#
log_must dd if=/dev/urandom of=$TESTDIR/src2 bs=1024k count=1
merged=$(kstat zil.zil_replay_split_merged)
dropped=$(kstat zil.zil_replay_split_dropped)
write_frozen_and_replay $TESTDIR/src2 /$TESTPOOL/$TESTFS/file2 lose
merged=$(( $(kstat zil.zil_replay_split_merged) - merged ))
dropped=$(( $(kstat zil.zil_replay_split_dropped) - dropped ))
log_note "zil_replay_split_merged went up by $merged," \
    "zil_replay_split_dropped by $dropped"
# Records past the last one replayed may be past the end of the file, so
# compare a copy of it extended with zeros to 1M.
log_must cp /$TESTPOOL/$TESTFS/file2 $TESTDIR/file2
log_must truncate -s 1M $TESTDIR/file2
whole=0
for i in 0 1 2 3 4 5 6 7; do
	log_must eval "dd if=$TESTDIR/src2 of=$TESTDIR/want bs=128k skip=$i \
	    count=1"
	log_must eval "dd if=$TESTDIR/file2 of=$TESTDIR/got bs=128k skip=$i \
	    count=1"
	if cmp -s $TESTDIR/want $TESTDIR/got; then
		log_must test $whole -eq $i
		(( whole += 1 ))
	elif (( $(tr -d '\0' <$TESTDIR/got | wc -c) != 0 )); then
		log_fail "record $i is neither the data written nor zeros"
	fi
done
log_note "$whole of 8 records replayed"
log_must test $whole -ge 1 -a $whole -lt 8
log_must test $merged -eq $whole
log_must test $dropped -ge 1

log_pass "Replay applies a write split across log blocks whole or not at all."
