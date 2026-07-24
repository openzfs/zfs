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
# http://www.illumos.org/license/CDDL.
#

#
# Copyright (c) 2026 Klara, Inc.
#

. $STF_SUITE/tests/functional/zstream/zstream.kshlib

#
# Description:
# Verify that "zstream raw" can recreate a zvol from a stream
#
# Strategy:
# 1. Create a zvol and initialize it with some data
# 2. Send the zvol and transform it into an image file with zstream raw
# 3. Verify the contents of the image file match the zvol
# 4. Add changes to the zvol and verify an incremental stream
# 5. Repeat for a stream package with multiple intermediary snapshots on a dev
#

verify_runnable "both"

log_assert "Verify zstream raw recreates a zvol from a stream"

typeset lodev=

function attach_img
{
	typeset img=$1

	if is_linux; then
		lodev=$(losetup -f $img --show)
	elif is_freebsd; then
		lodev=/dev/$(mdconfig $img)
	else
		lodev=$(lofiadm -a $img)
	fi
	block_device_wait $lodev
}

function detach_img
{
	if is_linux; then
		log_must losetup -d $lodev
	elif is_freebsd; then
		log_must mdconfig -du $lodev
	else
		log_must lofiadm -d $lodev
	fi
	lodev=
}

typeset fstype
typeset fsopts
typeset mntopts
if is_linux; then
	fstype="ext4"
	fsopts="-t $fstype"
	mntopts="-o discard"
else
	fstype=$NEWFS_DEFAULT_FS
	fsopts="-t"
	mntopts="-t $fstype"
fi

function cleanup
{
	if [[ -n $lodev ]]; then
		detach_img
	fi
	if ismounted $TESTDIR $fstype; then
		log_must umount $TESTDIR
	fi
	cleanup_pool $POOL
}

log_onexit cleanup

typeset volume=$POOL/zvol
typeset volsize=256m
typeset image=$BACKDIR/zvol.img
typeset image1=$BACKDIR/zvol1.img
typeset -i maxsz=$((1 << 20)) # 1MiB

function exercise_volume
{
	typeset -i j

	block_device_wait $ZVOL_DEVDIR/$volume
	log_must mount $mntopts $ZVOL_DEVDIR/$volume $TESTDIR
	for (( j = 0; j < 20; j++ )); do
		typeset f=$TESTDIR/file-$RANDOM
		log_must mkfile -n $maxsz $f
		log_must randwritecomp $f 100
	done
	log_must rm $(find $TESTDIR -type f | sort -R | head)
	(( len = RANDOM % maxsz ))
	(( start = RANDOM % len ))
	(( num = 1 + RANDOM % (len - start - 1) ))
	log_must randfree_file -l $len -s $start -n $num $TESTDIR/free-$RANDOM
	log_must umount $TESTDIR
	block_device_wait $ZVOL_DEVDIR/$volume
}

function compare_files
{
	typeset snapdev=$1
	typeset img=$2

	block_device_wait $snapdev
	log_must fsck -n $snapdev
	log_must mount $mntopts -o ro $snapdev $TESTDIR
	typeset volcksum=$(cat $TESTDIR/* | xxh128digest)
	log_must umount $TESTDIR

	log_must chmod -w $img
	attach_img $img
	log_must fsck -n $lodev
	log_must mount $mntopts -o ro $lodev $TESTDIR
	typeset imgcksum=$(cat $TESTDIR/* | xxh128digest)
	log_must umount $TESTDIR
	detach_img
	log_must chmod +w $img

	log_must test $volcksum = $imgcksum
}

# 1. Create a zvol and initialize it with some data
log_must zfs create -V $volsize -o snapdev=visible $volume
block_device_wait $ZVOL_DEVDIR/$volume
log_must new_fs $fsopts $ZVOL_DEVDIR/$volume
exercise_volume

log_note "Initial send"

# 2. Send the zvol and transform it into an image file with zstream raw
log_must zfs snapshot $volume@snapshot
log_must eval "guid=\$(zfs send -ceL $volume@snapshot | zstream raw $image)"

# 3. Verify the contents of the image file match the zvol
compare_files $ZVOL_DEVDIR/$volume@snapshot $image

log_note "Single incremental send"

# 4. Add changes to the zvol and verify an incremental stream
exercise_volume
log_must zfs snapshot $volume@snapshot1
log_must eval "guid=\$(zfs send -ceL -i @snapshot $volume@snapshot1 |
    zstream raw -g $guid $image)"
compare_files $ZVOL_DEVDIR/$volume@snapshot1 $image

# 5. Repeat for a stream package with multiple intermediary snapshots
log_must cp $image $image1
typeset -i nsnaps=5
typeset -i i
for i in $(seq 2 $nsnaps); do
	exercise_volume
	log_must zfs snapshot $volume@snapshot$i
	# Also verify consecutively applied individual incremental streams.
	log_must eval "zfs send -ceL -i @snapshot$((i - 1)) $volume@snapshot$i |
	    zstream raw $image1 2>&1"
	compare_files $ZVOL_DEVDIR/$volume@snapshot$i $image1
done

log_note "Ranged incremental send"

# Use multiple buffers to exercise write coalescing and a loopback device to
# exercise BLKDISCARD/DELETE here.
attach_img $image
log_must eval "zfs send -ceL -I @snapshot1 $volume@snapshot$nsnaps |
    zstream raw -b 4 -g $guid $lodev 2>&1"
detach_img
compare_files $ZVOL_DEVDIR/$volume@snapshot$nsnaps $image

log_pass "zstream raw recreates a zvol from a stream."
