#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or https://opensource.org/licenses/CDDL-1.0.
# See the License for the specific language governing permissions
# and limitations under the License.
#
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
# CDDL HEADER END
#

#
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Copyright (c) 2017 by Lawrence Livermore National Security, LLC.
# Use is subject to license terms.
#

. $STF_SUITE/tests/functional/slog/slog.kshlib

#
# DESCRIPTION:
#	Verify slogs are replayed correctly for a volume.
#
#	The general idea is to build up an intent log from a bunch of
#	diverse user commands without actually committing them to the
#	file system.  Then generate checksums for files and volume,
#	replay the intent log and verify the checksums.
#
#	To enable this automated testing of the intent log some minimal
#	support is required of the file system.  In particular, a
#	"freeze" command is required to flush the in-flight transactions;
#	to stop the actual committing of transactions; and to ensure no
#	deltas are discarded. All deltas past a freeze point are kept
#	for replay and comparison later. Here is the flow:
#
# STRATEGY:
#	1. Create an empty volume (TESTVOL), set sync=always, and format
#	   it with an ext4 filesystem and mount it.
#	2. Freeze TESTVOL.
#	3. Create log records of various types to verify replay.
#	4. Generate checksums for all ext4 files.
#	5. Unmount filesystem and export the pool
#	   <at this stage TESTVOL is empty again and unfrozen, and the
#	   intent log contains a complete set of deltas to replay it>
#	6. Import TESTVOL <which replays the intent log> and mount it.
#	7. Verify the stored checksums
#

verify_runnable "global"

VOLUME=$ZVOL_DEVDIR/$TESTPOOL/$TESTVOL
MNTPNT=$TESTDIR/$TESTVOL
FSTYPE=none

function cleanup_volume
{
	if ismounted $MNTPNT $FSTYPE; then
		log_must umount $MNTPNT
		rmdir $MNTPNT
	fi

	rm -f $TESTDIR/checksum.files

	cleanup
}

log_assert "Replay of intent log succeeds."
log_onexit cleanup_volume
log_must setup

#
# 1. Create an empty volume (TESTVOL), set sync=always, and format
#    it with an ext4 filesystem and mount it.
#
log_must zpool create $TESTPOOL ${DISKS%% *}
log_must zfs create -V 128M $TESTPOOL/$TESTVOL
log_must zfs set compression=on $TESTPOOL/$TESTVOL
log_must zfs set sync=always $TESTPOOL/$TESTVOL
log_must mkdir -p $TESTDIR
block_device_wait
if is_linux; then
	# ext4 only on Linux
	log_must new_fs -t ext4 -v $VOLUME
	log_must mkdir -p $MNTPNT
	log_must mount -o discard $VOLUME $MNTPNT
	FSTYPE=ext4
	log_must rmdir $MNTPNT/lost+found
else
	log_must new_fs $VOLUME
	log_must mkdir -p $MNTPNT
	log_must mount $VOLUME $MNTPNT
	FSTYPE=$NEWFS_DEFAULT_FS
fi
sync_all_pools

#
# 2. Freeze TESTVOL
#
log_must zpool freeze $TESTPOOL

#
# 3. Create log records of various types to verify replay.
#

# TX_WRITE
log_must dd if=/dev/urandom of=$MNTPNT/latency-8k bs=8k count=1 oflag=sync
log_must dd if=/dev/urandom of=$MNTPNT/latency-128k bs=128k count=1 oflag=sync

# TX_WRITE (WR_INDIRECT)
log_must zfs set logbias=throughput $TESTPOOL/$TESTVOL
log_must dd if=/dev/urandom of=$MNTPNT/throughput-8k bs=8k count=1
log_must dd if=/dev/urandom of=$MNTPNT/throughput-128k bs=128k count=1

# TX_WRITE (holes)
log_must dd if=/dev/urandom of=$MNTPNT/holes bs=128k count=8
log_must dd if=/dev/zero of=$MNTPNT/holes bs=128k count=2 seek=2 conv=notrunc

if is_linux; then
	# TX_TRUNCATE
	if fallocate --punch-hole 2>&1 | grep -q "unrecognized option"; then
		log_note "fallocate(1) does not support --punch-hole"
	else
		log_must dd if=/dev/urandom of=$MNTPNT/discard bs=128k count=16
		log_must fallocate --punch-hole -l 128K -o 512K $MNTPNT/discard
		log_must fallocate --punch-hole -l 512K -o 1M $MNTPNT/discard
	fi
fi

#
# 4. Generate checksums for all ext4 files.
#
typeset checksum=$(cat $MNTPNT/* | xxh128digest)

#
# 5. Unmount filesystem and export the pool
#
# At this stage TESTVOL is initialized with the random data and frozen,
# the intent log contains a complete set of deltas to replay.
#
log_must umount $MNTPNT

log_note "Verify transactions to replay:"
log_must zdb -iv $TESTPOOL/$TESTVOL

log_must zpool export $TESTPOOL

#
# 6. Import TESTPOOL, the intent log is replayed during minor creation.
#
# Import the pool to unfreeze it and claim log blocks.  It has to be
# `zpool import -f` because we can't write a frozen pool's labels!
#
log_must zpool import -f $TESTPOOL
block_device_wait
log_must mount $VOLUME $MNTPNT

#
# 7. Verify the stored checksums
#
log_note "Verify current block usage:"
log_must zdb -bcv $TESTPOOL

log_note "Verify checksums"
typeset checksum1=$(cat $MNTPNT/* | xxh128digest)
[[ "$checksum1" == "$checksum" ]] || \
    log_fail "checksum mismatch ($checksum1 != $checksum)"

log_pass "Replay of intent log succeeds."
