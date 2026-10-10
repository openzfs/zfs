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

. $STF_SUITE/tests/functional/zstream/zstream.kshlib

#
# Description:
# Verify that "zstream raw" applies buffered writes before later frees and
# resizes in a stream package.
#
# Strategy:
# 1. Create a zvol and convert an empty snapshot to an image file
# 2. Write a block, snapshot, then zero it so it becomes a hole, snapshot
# 3. Apply the -I stream package to the image and verify it matches the zvol
# 4. Write the last block, snapshot, then shrink the zvol, snapshot
# 5. Apply the -I stream package to the image and verify it matches the zvol
# 6. Repeat step 3 against the image attached as a block device
#

verify_runnable "both"

log_assert "Verify zstream raw orders buffered writes before frees and resizes"

typeset lodev=

function cleanup
{
	if [[ -n $lodev ]]; then
		if is_linux; then
			log_must losetup -d $lodev
		elif is_freebsd; then
			log_must mdconfig -du $lodev
		else
			log_must lofiadm -d $lodev
		fi
	fi
	cleanup_pool $POOL
}

log_onexit cleanup

typeset volume=$POOL/raw002vol
typeset voldev=$ZVOL_DEVDIR/$volume
typeset image=$BACKDIR/raw002vol.img
typeset -i bs=16384

function write_block # data block
{
	log_must dd if=$1 of=$voldev bs=$bs count=1 seek=$2 conv=fsync
}

function compare_image # snapshot image
{
	block_device_wait $voldev@$1
	log_must cmp $voldev@$1 $2
}

# 1. Create a zvol and convert an empty snapshot to an image file
#
# Compression turns the zeroed block below into a hole, so that the
# incremental stream frees it.
log_must zfs create -V 64m -o volblocksize=$bs -o compression=lz4 \
    -o snapdev=visible $volume
block_device_wait $voldev
log_must zfs snapshot $volume@a
log_must eval "guid=\$(zfs send $volume@a | zstream raw $image)"
compare_image a $image
log_must cp $image $image.a

# 2. Write a block, snapshot, then zero it so it becomes a hole, snapshot
write_block /dev/urandom 0
log_must zfs snapshot $volume@b
write_block /dev/zero 0
log_must zfs snapshot $volume@c

# 3. Apply the -I stream package to the image and verify it matches the zvol
#
# The write from @b is still buffered when the free from @c is applied, and
# must not land on top of it.
log_must eval "guid=\$(zfs send -I @a $volume@c | zstream raw -g $guid $image)"
compare_image c $image

# 4. Write the last block, snapshot, then shrink the zvol, snapshot
write_block /dev/urandom $(( (64 << 20) / bs - 1 ))
log_must zfs snapshot $volume@d
log_must zfs set volsize=32m $volume
log_must zfs snapshot $volume@e

# 5. Apply the -I stream package to the image and verify it matches the zvol
#
# The write from @d lies beyond the new end of the volume, and must not
# extend the image again after it is truncated.
log_must eval "zfs send -I @c $volume@e | zstream raw -g $guid $image"
compare_image e $image

# 6. Repeat step 3 against the image attached as a block device
log_must mv $image.a $image
if is_linux; then
	lodev=$(losetup -f $image --show)
elif is_freebsd; then
	lodev=/dev/$(mdconfig $image)
else
	lodev=$(lofiadm -a $image)
fi
block_device_wait $lodev
log_must eval "zfs send -I @a $volume@c | zstream raw $lodev"
compare_image c $lodev

log_pass "zstream raw orders buffered writes before frees and resizes"
