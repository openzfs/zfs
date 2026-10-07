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
#	Verify that a split write is replayed when the file was truncated
#	into it before the log was written.
#
# STRATEGY:
#	1. Create a pool with a slog, so async writes are logged with
#	   their data (WR_NEED_COPY), and freeze it.
#	2. Write 1M in one write(2), then truncate the file to a size
#	   inside the first piece of one of its 128K records, and fsync(2)
#	   it to write the log.  By then, the pieces of that record past
#	   the new end of file no longer exist.
#	3. Export and import to replay the log.  The file must hold the
#	   data written below the new end of file, and no piece may have
#	   been dropped.
#	4. Repeat for the first and the last record.
#

verify_runnable "global"

#
# Freeze the pool, write $1 to $2 in one write(2), truncate $2 to $3 bytes
# and fsync it, then replay the log.  (Unmounting doesn't write the log on
# FreeBSD: the file's vnode is gone by the time the log is committed, and
# its data can't be read.)
#
function write_truncate_frozen_and_replay
{
	typeset src=$1
	typeset dst=$2
	typeset size=$3

	# Make sure the dataset has a ZIL header before freezing.
	log_must dd if=/dev/zero of=/$TESTPOOL/$TESTFS/sync \
	    conv=fdatasync,fsync bs=1 count=1
	log_must zpool freeze $TESTPOOL
	log_must dd if=$src of=$dst bs=1024k count=1
	log_must dd if=/dev/null of=$dst bs=1 seek=$size conv=fsync
	log_must zfs unmount $TESTPOOL/$TESTFS
	log_must zpool export $TESTPOOL
	log_must zpool import -f -d $VDIR $TESTPOOL
}

log_assert "Replay keeps a split write whose tail was truncated away."
log_onexit cleanup
log_must setup

log_must zpool create $TESTPOOL $VDEV log mirror $LDEV
log_must zfs create -o compression=off -o recordsize=128k \
    $TESTPOOL/$TESTFS
log_must mkdir -p $TESTDIR
log_must dd if=/dev/urandom of=$TESTDIR/src bs=1024k count=1

#
# The first piece of the first record is short and that of the last record
# is long, so truncate into each of them.
#
for size in 1024 1000000; do
	log_must eval "dd if=$TESTDIR/src bs=$size count=1 \
	    >$TESTDIR/expect.$size"
	dropped=$(kstat zil.zil_replay_split_dropped)
	write_truncate_frozen_and_replay $TESTDIR/src \
	    /$TESTPOOL/$TESTFS/file.$size $size
	log_must cmp $TESTDIR/expect.$size /$TESTPOOL/$TESTFS/file.$size
	dropped=$(( $(kstat zil.zil_replay_split_dropped) - dropped ))
	log_note "zil_replay_split_dropped went up by $dropped"
	log_must test $dropped -eq 0
done

log_pass "Replay keeps a split write whose tail was truncated away."
