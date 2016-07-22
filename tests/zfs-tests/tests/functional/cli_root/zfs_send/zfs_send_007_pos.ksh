#!/bin/ksh
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
	$ZFS destroy -rf $TESTPOOL/fs
	$ZFS destroy -rf $TESTPOOL/recvfs
	$RM $streamfile
	$RM $vdev
	$ZPOOL destroy testpool
}


log_assert "Verify that 'zfs send' drills appropriate holes"
log_onexit cleanup
streamfile=$(mktemp /var/tmp/file.XXXXXX)
vdev=$(mktemp /var/tmp/file.XXXXXX)


test_pool ()
{
	POOL=$1
	log_must $ZFS create -o recordsize=512 $POOL/fs
	mntpnt=$(get_prop mountpoint "$POOL/fs")
	log_must $DD if=/dev/urandom of=${mntpnt}/file bs=512 count=1 2>/dev/null
	first_object=$(ls -i $mntpnt | awk '{print $1}')
	log_must $ZFS snapshot $POOL/fs@a
	while true; do
		log_must $FIND $mntpnt -delete
		sync
		log_must $MKFILES "$mntpnt/" 4000
		FILE=$(ls -i $mntpnt | awk \
			'{if ($1 == '$first_object') {print $2}}')
		if [[ -n "$FILE" ]]; then
			break
		fi
	done
	$DD if=/dev/urandom of=${mntpnt}/$FILE bs=512 count=1 seek=1 2>/dev/null

	log_must $ZFS snapshot $POOL/fs@b

	log_must eval "$ZFS send $POOL/fs@a > $streamfile"
	$CAT $streamfile | log_must $ZFS receive $POOL/recvfs

	log_must eval "$ZFS send -i @a $POOL/fs@b > $streamfile"
	$CAT $streamfile | log_must $ZFS receive $POOL/recvfs

	recv_mntpnt=$(get_prop mountpoint "$POOL/recvfs")
	log_must $DIFF -r $mntpnt $recv_mntpnt
	log_must $ZFS destroy -rf $POOL/fs
	log_must $ZFS destroy -rf $POOL/recvfs
}

test_pool $TESTPOOL
log_must $TRUNCATE --size=1G $vdev
log_must $ZPOOL create -o version=1 testpool $vdev
test_pool testpool
log_must $ZPOOL destroy testpool
log_must $ZPOOL create -d testpool $vdev
test_pool testpool
log_must $ZPOOL destroy testpool


log_pass "'zfs send' drills appropriate holes"
