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

# Copyright (c) 2025 by Lawrence Livermore National Security, LLC.

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Verify that we can make an xfs filesystem on a ZFS-backed loopback device.
#
# See:
# https://github.com/openzfs/zfs/pull/17298
# https://github.com/openzfs/zfs/issues/17277
#
# STRATEGY:
# 1. Make a pool
# 2. Make a file on the pool or create zvol
# 3. Mount the file/zvol behind a loopback device
# 4. Create & mount an xfs filesystem on the loopback device

function cleanup
{
	if [ -d $TEST_BASE_DIR/mnt ] ; then
		umount $TEST_BASE_DIR/mnt
		log_must rmdir $TEST_BASE_DIR/mnt
	fi
	if [ -n "$DEV" ] ; then
		log_must losetup -d $DEV
	fi
	destroy_pool $TESTPOOL2
	log_must rm -f $TEST_BASE_DIR/file1
}

if [ ! -x "$(which mkfs.xfs)" ] ; then
	log_unsupported "No mkfs.xfs binary"
fi

if [ ! -d /lib/modules/$(uname -r)/kernel/fs/xfs ] && \
     ! grep -qE '\sxfs$' /proc/filesystems ; then
	log_unsupported "No XFS kernel support"
fi

log_assert "Make an xfs filesystem on a ZFS-backed loopback device"
log_onexit cleanup

# fio options
export NUMJOBS=2
export RUNTIME=3
export PERF_RANDSEED=1234
export PERF_COMPPERCENT=66
export PERF_COMPCHUNK=0
export BLOCKSIZE=128K
export SYNC_TYPE=0
export FILE_SIZE=$(( 1024 * 1024 ))

function do_test
{
	imgfile=$1
	log_note "Running test on $imgfile"
	log_must losetup -f $imgfile
	DEV=$(losetup --associated $imgfile | grep -Eo '^/dev/loop[0-9]+')
	log_must mkfs.xfs $DEV
	mkdir $TEST_BASE_DIR/mnt
	log_must mount $DEV $TEST_BASE_DIR/mnt
	export DIRECTORY=$TEST_BASE_DIR/mnt

	for d in 0 1 ; do
		# fio options
		export DIRECT=$d
		log_must fio $FIO_SCRIPTS/mkfiles.fio
		log_must fio $FIO_SCRIPTS/random_reads.fio
	done
	log_must umount $TEST_BASE_DIR/mnt
	log_must rmdir $TEST_BASE_DIR/mnt
	log_must losetup -d $DEV
	DEV=""
}

log_must truncate -s 1G $TEST_BASE_DIR/file1
log_must zpool create $TESTPOOL2 $TEST_BASE_DIR/file1
log_must truncate -s 512M /$TESTPOOL2/img
do_test /$TESTPOOL2/img
log_must rm /$TESTPOOL2/img
log_must zfs create -V 512M $TESTPOOL2/vol

blkdev="$ZVOL_DEVDIR/$TESTPOOL2/vol"
block_device_wait $blkdev
do_test $blkdev

log_pass "Verified xfs filesystem on a ZFS-backed loopback device"
