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
# Copyright (c) 2026 by Will Rouesnel. All rights reserved.
#

. $STF_SUITE/tests/functional/slog/slog.kshlib

#
# DESCRIPTION:
#	Replay must not write zeros for an indirect (WR_INDIRECT) write
#	whose dmu_sync() block could not be allocated, and must not lose a
#	later write whose commit was acknowledged by the log.
#
#	An indirect TX_WRITE record holds the block pointer of a dmu_sync()
#	write, which is a child of the log block's write.  If that
#	allocation fails, the block pointer is left a hole (or, if the
#	write ganged and its members failed, points to an incomplete gang
#	block), and the log block must not be written with it: replay reads
#	a hole as zeros, so it would replace the file's data with zeros.
#	A log block that is not written breaks the chain to the blocks after
#	it, so a later commit must not be acknowledged by them alone.
#
# STRATEGY:
#	For a dmu_sync() write and its gang header that both fail to
#	allocate, and for one whose gang header allocates but whose members
#	don't:
#	1. Create a pool with a small normal vdev for file data, a special
#	   vdev for metadata and small blocks and a log vdev, and a file of 4
#	   records.
#	2. Freeze the pool.  Txgs still sync, but frees are not returned to
#	   the metaslabs, so overwriting a filler file uses up space in the
#	   normal class.  Do that until it has exactly 128K free, or 136K
#	   for the gang case.
#	3. Overwrite one record of the file with O_SYNC.  logbias=throughput
#	   logs it indirect.  The frozen pool first syncs the txg holding the
#	   write, which uses up 128K, so the dmu_sync() write of the block
#	   fails to allocate.
#	4. Write a small new file with O_SYNC and logbias=latency, which
#	   logs its data, and note whether its commit fell back to waiting
#	   for the txg to sync (zil_commit_error_count).
#	5. Export and import the pool, which replays the log.
#	6. The record must hold either the old or the new data, not zeros.
#	   The new file must be there, unless its commit fell back to the txg
#	   (which a frozen pool does not write).
#

verify_runnable "global"

function cleanup_fs
{
	cleanup
}

function nfree
{
	zpool list -Hpv $TESTPOOL | awk -v d=$VDIR/a '$1 == d { print $4 }'
}

log_assert "Replay of an indirect write whose block failed to allocate" \
    "doesn't write zeros or lose later writes."
log_onexit cleanup_fs

typeset file=/$TESTPOOL/$TESTFS/file
typeset later=/$TESTPOOL/$TESTFS/later
typeset -i f n i e0 e1 extra target

for extra in 0 8192; do
	((target = 131072 + extra))
	log_note "Normal vdev left with $target bytes free"
	poolexists $TESTPOOL && destroy_pool $TESTPOOL
	log_must setup

	#
	# 1. The pool, with file data alone in the normal vdev.
	#
	log_must truncate -s 128m $VDIR/a
	log_must truncate -s 512m $VDIR/d
	log_must zpool create -o ashift=12 $TESTPOOL $VDIR/a \
	    special $VDIR/d log $VDIR/e
	log_must zfs create -o recordsize=128k -o compression=off \
	    -o logbias=throughput -o special_small_blocks=4k \
	    $TESTPOOL/$TESTFS
	log_must zfs create -o recordsize=4k -o compression=off \
	    $TESTPOOL/fill

	log_must rm -rf $TESTDIR
	log_must mkdir -p $TESTDIR
	log_must dd if=/dev/urandom of=$TESTDIR/old bs=128k count=4
	log_must dd if=/dev/urandom of=$TESTDIR/new bs=128k count=1
	log_must dd if=/dev/urandom of=$TESTDIR/later bs=4k count=1
	log_must dd if=/dev/zero of=$TESTDIR/zeros bs=128k count=1
	log_must cp $TESTDIR/old $file
	log_must dd if=/dev/urandom of=/$TESTPOOL/fill/f bs=4k count=8192

	# Create the ZIL header, so that log records are written after the
	# freeze.
	log_must dd if=/dev/zero of=/$TESTPOOL/$TESTFS/sync \
	    conv=fdatasync,fsync bs=1 count=1
	log_must sync_pool $TESTPOOL

	#
	# 2. Freeze, and use up all but $target bytes of the normal vdev.
	#
	log_must zpool freeze $TESTPOOL
	for ((i = 0; i < 100; i++)); do
		f=$(nfree)
		((f <= target)) && break
		((n = (f - target) / 4096))
		((n > 8192)) && n=8192
		log_must dd if=/dev/urandom of=/$TESTPOOL/fill/f bs=4k \
		    count=$n conv=notrunc
		log_must sync_pool $TESTPOOL
	done
	f=$(nfree)
	log_note "normal vdev free: $f"
	((f == target)) || log_unsupported "could not leave $target free"

	#
	# 3. Overwrite record 1 synchronously.
	#
	log_must dd if=$TESTDIR/new of=$file bs=128k seek=1 count=1 \
	    conv=notrunc oflag=sync
	log_note "normal vdev free: $(nfree)"

	#
	# 4. A later synchronous write, its data in the log.
	#
	log_must zfs set logbias=latency $TESTPOOL/$TESTFS
	e0=$(kstat zil.zil_commit_error_count)
	log_must dd if=$TESTDIR/later of=$later bs=4k count=1 oflag=sync
	e1=$(kstat zil.zil_commit_error_count)
	log_must zfs unmount /$TESTPOOL/$TESTFS
	log_note "Transactions to replay:"
	log_must zdb -iv $TESTPOOL/$TESTFS

	#
	# 5. Export and import, replaying the log.
	#
	log_must zpool export $TESTPOOL
	log_must zpool import -f -d $VDIR $TESTPOOL

	#
	# 6. Record 1 holds the old or the new data; record 0 is untouched.
	#    The later write is there, unless its commit fell back to the txg.
	#
	log_must dd if=$file of=$TESTDIR/r1 bs=128k skip=1 count=1
	log_must dd if=$TESTDIR/old of=$TESTDIR/old1 bs=128k skip=1 count=1
	if cmp -s $TESTDIR/r1 $TESTDIR/zeros; then
		log_fail "Record 1 was replayed as zeros"
	fi
	if ! cmp -s $TESTDIR/r1 $TESTDIR/old1 && \
	    ! cmp -s $TESTDIR/r1 $TESTDIR/new; then
		log_fail "Record 1 holds neither the old nor the new data"
	fi
	log_must dd if=$file of=$TESTDIR/r0 bs=128k count=1
	log_must dd if=$TESTDIR/old of=$TESTDIR/old0 bs=128k count=1
	log_must cmp $TESTDIR/r0 $TESTDIR/old0
	if ! cmp -s $later $TESTDIR/later; then
		((e1 > e0)) || log_fail "Acknowledged later write was lost"
		log_note "Later write not replayed; its commit fell back"
	fi
done

log_pass "Replay of an indirect write whose block failed to allocate" \
    "doesn't write zeros or lose later writes."
