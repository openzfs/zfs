#!/bin/ksh
# SPDX-License-Identifier: CDDL-1.0
#
# CDDL HEADER START
#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# http://www.illumos.org/license/CDDL.
#
# CDDL HEADER END
#

#
# Copyright (c) 2015, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
#
# DESCRIPTION:
#	Verify 'zfs send' drills holes appropriately when files are replaced
#
# STRATEGY:
#	1. Create dataset
#	2. Write block 0 in a bunch of files
#	3. Snapshot the dataset
#	4. Remove all the files and rewrite some files with just block 1
#	5. Snapshot the dataset
#	6. Send both snapshots and receive them locally
#	7. diff the received dataset and the old datasets.
#	8. Repeat steps 1-7 above with pool that never had hole birth enabled.
#

verify_runnable "both"

function cleanup
{
	zfs destroy -rf $TESTPOOL/fs
	zfs destroy -rf $TESTPOOL/recvfs
	rm $streamfile
	rm $vdev
	zpool destroy tmp_pool
}


log_assert "Verify that 'zfs send' drills appropriate holes"
log_onexit cleanup
streamfile=$(mktemp)
vdev=$(mktemp)


function test_pool
{
	POOL=$1
	log_must zfs create -o recordsize=512 $POOL/fs
	mntpnt=$(get_prop mountpoint "$POOL/fs")
	log_must eval "dd if=/dev/urandom of=${mntpnt}/file bs=512 count=1 2>/dev/null"
	object=$(ls -i $mntpnt | awk '{print $1}')
	log_must zfs snapshot $POOL/fs@a
	while true; do
		log_must find $mntpnt/ -type f -delete
		sync_all_pools
		log_must mkfiles "$mntpnt/" 4000
		sync_all_pools
		# check if we started reusing objects
		object=$(ls -i $mntpnt | sort -n | awk -v object=$object \
		    '{if ($1 <= object) {exit 1}} END {print $1}') || break
	done
	dd if=/dev/urandom of=${mntpnt}/$FILE bs=512 count=1 seek=1 2>/dev/null

	log_must zfs snapshot $POOL/fs@b

	log_must eval "zfs send $POOL/fs@a > $streamfile"
	cat $streamfile | log_must zfs receive $POOL/recvfs

	log_must eval "zfs send -i @a $POOL/fs@b > $streamfile"
	cat $streamfile | log_must zfs receive $POOL/recvfs

	recv_mntpnt=$(get_prop mountpoint "$POOL/recvfs")
	log_must directory_diff $mntpnt $recv_mntpnt
	log_must zfs destroy -rf $POOL/fs
	log_must zfs destroy -rf $POOL/recvfs
}

test_pool $TESTPOOL
log_must truncate -s 1G $vdev
log_must zpool create -o version=1 tmp_pool $vdev
test_pool tmp_pool
log_must zpool destroy tmp_pool
log_must zpool create -d tmp_pool $vdev
test_pool tmp_pool
log_must zpool destroy tmp_pool

log_pass "'zfs send' drills appropriate holes"
