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
#	Replay of an indirect write into the last, partial block of a file
#	must not change the file's size.
#
#	An indirect (WR_INDIRECT) TX_WRITE points to a copy of the whole
#	block.  Replay used to write the whole block, which grew the file
#	to the block boundary when the write ended before EOF.
#
# STRATEGY:
#	1. Create a file whose last 128K block is partial
#	2. Stop txgs from syncing on their own
#	3. Write 4K inside the last block, ending before EOF, with
#	   logbias=throughput so the write is logged indirect, and fsync
#	4. Copy the file, and save the vdevs, which then hold the log but
#	   not the write, as after a crash
#	5. Restore the saved vdevs and import the pool to replay the log
#	6. Compare the file against the copy
#

verify_runnable "global"

function cleanup_fs
{
	restore_tunable TXG_TIMEOUT
	cleanup
	rm -f $VDIR2/a.save $VDIR2/e.save
}

log_assert "Replay of an indirect write keeps the file size."
log_onexit cleanup_fs
log_must setup
log_must save_tunable TXG_TIMEOUT

typeset file=/$TESTPOOL/$TESTFS/file
typeset -i size=1000000
set -A vdevs $VDIR/a $VDIR/e

log_must zpool create $TESTPOOL ${vdevs[0]} log ${vdevs[1]}
log_must zfs create -o compression=off -o recordsize=128k \
    -o logbias=throughput $TESTPOOL/$TESTFS

#
# 1. The last 128K block (at 917504) holds 82496 bytes.
#
log_must dd if=/dev/urandom of=$file bs=1000 count=$((size / 1000))

# Create the ZIL header now, so that the fsync below doesn't sync a txg.
log_must dd if=/dev/zero of=/$TESTPOOL/$TESTFS/sync \
    oflag=sync bs=1 count=1
log_must sync_pool $TESTPOOL

#
# 2. Stop txgs from syncing on their own.  The sync thread picks up the new
#    timeout after the next txg.
#
log_must set_tunable32 TXG_TIMEOUT 3600
log_must sync_pool $TESTPOOL

#
# 3. 4K at 942080 (block 230 of 4K), within the last block and before EOF.
#
log_must dd if=/dev/urandom of=$file bs=4k count=1 seek=230 \
    conv=notrunc,fsync

#
# 4. Copy the file and save the vdevs, then let the pool go.
#
log_must mkdir -p $TESTDIR/copy
log_must cp $file $TESTDIR/copy/file
log_must cp ${vdevs[0]} $VDIR2/a.save
log_must cp ${vdevs[1]} $VDIR2/e.save
log_must restore_tunable TXG_TIMEOUT
log_must zpool export $TESTPOOL

#
# 5. Restore the saved vdevs and replay the log.
#
log_must cp $VDIR2/a.save ${vdevs[0]}
log_must cp $VDIR2/e.save ${vdevs[1]}

log_note "Verify an indirect TX_WRITE to replay:"
typeset recs=$(zdb -e -p $VDIR -ivvvvv $TESTPOOL/$TESTFS)
echo "$recs"
echo "$recs" | grep -q "has blkptr" || \
    log_fail "the log has no indirect TX_WRITE"

log_must zpool import -d $VDIR $TESTPOOL

#
# 6. Compare.
#
typeset -i newsize=$(stat_size $file)
(( newsize == size )) || log_fail "file size $newsize, expected $size"
log_must cmp $file $TESTDIR/copy/file

log_pass "Replay of an indirect write keeps the file size."
