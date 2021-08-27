#!/bin/ksh -p
#
# CDDL HEADER START
#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy is of the CDDL is also available via the Internet
# at http://www.illumos.org/license/CDDL.
#
# CDDL HEADER END
#

#
# Copyright (c) 2018 Datto Inc.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#	zfs unmount should work on nested datasets
#
# STRATEGY:
#	1. Create a set of nested datasets
#	2. Unmount a nested dataset and make sure it is unmounted
#	3. Ensure the dataset deeper than the one above is also unmounted
#	4. Ensure the datasets shallower than the unmounted one is still mounted
#	5. Repeat from step 2 with other mountpoint values and shallower nesting
#

verify_runnable "both"

function nesting_cleanup
{
	log_must zfs destroy -fR $TESTPOOL/a
	log_must zfs destroy -fR $TESTPOOL/b
	log_must zfs destroy -fR $TESTPOOL/c
	log_must zfs destroy -fR $TESTPOOL/d
}

log_onexit nesting_cleanup

set -A test_depths 30 16 3
typeset mountpoint=/$TESTPOOL/mnt

dsA32=$(printf 'a/%.0s' {1..32})"a"
log_must zfs create -p $TESTPOOL/$dsA32

dsB32=$(printf 'b/%.0s' {1..32})"b"
log_must zfs create -o mountpoint=none -p $TESTPOOL/$dsB32
# FreeBSD's mount command ignores the mountpoint property.
if ! is_freebsd; then
	log_mustnot mount -t zfs $TESTPOOL/$dsB32 /mnt
fi

dsC32=$(printf 'c/%.0s' {1..32})"c"
log_must zfs create -o mountpoint=legacy -p $TESTPOOL/$dsC32
log_must mount -t zfs $TESTPOOL/$dsC32 /mnt

dsD32=$(printf 'd/%.0s' {1..32})"d"
log_must zfs create -o mountpoint=$mountpoint -p $TESTPOOL/$dsD32


for d in ${test_depths[@]}; do
	# default mountpoint
	ds_pre=$(printf 'a/%.0s' {1..$(($d-2))})"a"
	ds=$(printf 'a/%.0s' {1..$(($d-1))})"a"
	ds_post=$(printf 'a/%.0s' {1..$(($d))})"a"
	if ! ismounted $TESTPOOL/$ds_pre; then
		log_fail "$TESTPOOL/$ds_pre (pre) not initially mounted"
	fi
	if ! ismounted $TESTPOOL/$ds; then
		log_fail "$TESTPOOL/$ds not initially mounted"
	fi
	if ! ismounted $TESTPOOL/$ds_post; then
		log_fail "$TESTPOOL/$ds_post (post) not initially mounted"
	fi

	log_must zfs snapshot $TESTPOOL/$ds@snap
	# force snapshot mount in .zfs
	log_must ls /$TESTPOOL/$ds/.zfs/snapshot/snap
	log_must_nostderr zfs unmount $TESTPOOL/$ds

	if ! ismounted $TESTPOOL/$ds_pre; then
		log_fail "$ds_pre is not mounted"
	fi
	if ismounted $TESTPOOL/$ds; then
		log_fail "$ds is mounted"
	fi
	if ismounted $TESTPOOL/$ds_post; then
		log_fail "$ds_post (post) is mounted"
	fi


	# mountpoint=none
	ds_pre=$(printf 'b/%.0s' {1..$(($d-2))})"b"
	ds=$(printf 'b/%.0s' {1..$(($d-1))})"b"
	ds_post=$(printf 'b/%.0s' {1..$(($d))})"b"
	if ! ismounted $TESTPOOL/$ds_pre; then
		log_fail "$TESTPOOL/$ds_pre (pre) not initially mounted"
	fi
	if ! ismounted $TESTPOOL/$ds; then
		log_fail "$TESTPOOL/$ds not initially mounted"
	fi
	if ! ismounted $TESTPOOL/$ds_post; then
		log_fail "$TESTPOOL/$ds_post (post) not initially mounted"
	fi

	log_must zfs snapshot $TESTPOOL/$ds@snap
	# force snapshot mount in .zfs
	log_must ls /$TESTPOOL/$ds/.zfs/snapshot/snap
	log_must_nostderr zfs unmount $TESTPOOL/$ds

	if ! ismounted $TESTPOOL/$ds_pre; then
		log_fail "$TESTPOOL/$ds_pre (pre) not mounted"
	fi
	if ismounted $TESTPOOL/$ds; then
		log_fail "$TESTPOOL/$ds is mounted"
	fi
	if ismounted $TESTPOOL/$ds_post; then
		log_fail "$TESTPOOL/$ds_post (post) is mounted"
	fi


	# mountpoint=legacy
	ds_pre=$(printf 'c/%.0s' {1..$(($d-2))})"c"
	ds=$(printf 'c/%.0s' {1..$(($d-1))})"c"
	ds_post=$(printf 'c/%.0s' {1..$(($d))})"c"
	if ! ismounted $TESTPOOL/$ds_pre; then
		log_fail "$TESTPOOL/$ds_pre (pre) not initially mounted"
	fi
	if ! ismounted $TESTPOOL/$ds; then
		log_fail "$TESTPOOL/$ds not initially mounted"
	fi
	if ! ismounted $TESTPOOL/$ds_post; then
		log_fail "$TESTPOOL/$ds_post (post) not initially mounted"
	fi

	log_must zfs snapshot $TESTPOOL/$ds@snap
	# force snapshot mount in .zfs
	log_must ls /$TESTPOOL/$ds/.zfs/snapshot/snap
	log_must_nostderr zfs unmount $TESTPOOL/$ds

	if ! ismounted $TESTPOOL/$ds_pre; then
		log_fail "$TESTPOOL/$ds_pre (pre) not mounted"
	fi
	if ismounted $TESTPOOL/$ds; then
		log_fail "$TESTPOOL/$ds is mounted"
	fi
	if ismounted $TESTPOOL/$ds_post; then
		log_fail "$TESTPOOL/$ds_post (post) is mounted"
	fi


	# mountpoint=/testpool/mnt
	ds_pre=$(printf 'd/%.0s' {1..$(($d-2))})"d"
	ds=$(printf 'd/%.0s' {1..$(($d-1))})"d"
	ds_post=$(printf 'd/%.0s' {1..$(($d))})"d"
	if ! ismounted $TESTPOOL/$ds_pre; then
		log_fail "$TESTPOOL/$ds_pre (pre) not initially mounted"
	fi
	if ! ismounted $TESTPOOL/$ds; then
		log_fail "$TESTPOOL/$ds not initially mounted"
	fi
	if ! ismounted $TESTPOOL/$ds_post; then
		log_fail "$TESTPOOL/$ds_post (post) not initially mounted"
	fi

	log_must zfs snapshot $TESTPOOL/$ds@snap
	# force snapshot mount in .zfs
	log_must ls /$TESTPOOL/$ds/.zfs/snapshot/snap
	log_must_nostderr zfs unmount $TESTPOOL/$ds

	if ! ismounted $TESTPOOL/$ds_pre; then
		log_fail "$ds_pre is not mounted"
	fi
	if ismounted $TESTPOOL/$ds; then
		log_fail "$ds is mounted"
	fi
	if ismounted $TESTPOOL/$ds_post; then
		log_fail "$ds_post (post) is mounted"
	fi
done

log_must rmdir $mountpoint # remove the mountpoint we created
log_must zpool export $TESTPOOL
log_must zpool import $TESTPOOL

log_pass "Verified nested dataset are unmounted."
