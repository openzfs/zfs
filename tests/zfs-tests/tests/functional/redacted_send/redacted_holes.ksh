#!/bin/ksh
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
# Copyright (c) 2018 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/redacted_send/redacted.kshlib

#
# Description:
# Verify redacted send streams reliably handle holes.
#
# Strategy:
# 1. Holes written at the beginning and end of a non-sparse file in the
#    redacted list are correctly redacted.
# 2. Holes written throughout a non-sparse file in the redacted list are
#    correctly redacted.
# 3. Data written into a hole in a sparse file in the redacted list are
#    correctly redacted.
# 4. Holes in metadata blocks.
#

typeset ds_name="holes"
typeset sendfs="$POOL/$ds_name"
typeset recvfs="$POOL2/$ds_name"
typeset clone="$POOL/${ds_name}_clone"
typeset tmpdir="$(get_prop mountpoint $POOL)/tmp"
typeset stream=$(mktemp $tmpdir/stream.XXXX)
setup_dataset $ds_name '' setup_holes
typeset clone_mnt="$(get_prop mountpoint $clone)"
typeset send_mnt="$(get_prop mountpoint $sendfs)"
typeset recv_mnt="/$POOL2/$ds_name"
typeset M=$((1024 * 1024))

log_onexit redacted_cleanup $sendfs $recvfs

# Write holes at the start and end of a non-sparse file.
if is_illumos; then
	log_must mkholes -h 0:$M -h $((7 * M)):$M $clone_mnt/f1
else
	log_must dd if=/dev/zero of=$clone_mnt/f1 bs=1M count=1 conv=notrunc
	log_must dd if=/dev/zero of=$clone_mnt/f1 bs=1M count=1 conv=notrunc seek=7
fi
log_must zfs snapshot $clone@snap1
log_must zfs redact $sendfs@snap book1 $clone@snap1
log_must eval "zfs send --redact book1 $sendfs@snap >$stream"
log_must eval "zfs recv $recvfs <$stream"
compare_files $sendfs $recvfs "f1" "$RANGE5"
log_must zfs rollback -R $clone@snap
log_must zfs destroy -R $recvfs

# Write two overlapping sets of holes into the same non-sparse file.
log_must stride_dd -i /dev/zero -o $clone_mnt/f1 -b $((128 * 1024)) -c 8 -s 2 -k 3
log_must stride_dd -i /dev/zero -o $clone_mnt/f1 -b $((256 * 1024)) -c 8 -s 2 -k 6
log_must zfs snapshot $clone@snap1
log_must zfs redact $sendfs@snap book2 $clone@snap1
log_must eval "zfs send --redact book2 $sendfs@snap >$stream"
log_must eval "zfs recv $recvfs <$stream"
compare_files $sendfs $recvfs "f1" "$RANGE6"
log_must zfs rollback -R $clone@snap
log_must zfs destroy -R $recvfs

# Write data into the middle of a hole.
if is_illumos; then
	log_must mkholes -d $((3 * M)):$((2 * M)) $clone_mnt/f2
else
	log_must dd if=/dev/urandom of=$clone_mnt/f2 bs=1M count=2 seek=3 \
	    conv=notrunc
fi
log_must zfs snapshot $clone@snap1
log_must zfs redact $sendfs@snap book3 $clone@snap1
log_must eval "zfs send --redact book3 $sendfs@snap >$stream"
log_must eval "zfs recv $recvfs <$stream"
compare_files $sendfs $recvfs "f2" "$RANGE14"
log_must zfs rollback -R $clone@snap
log_must zfs destroy -R $recvfs

# Remove a file with holes.
log_must rm $clone_mnt/f3
log_must zfs snapshot $clone@snap1
log_must zfs redact $sendfs@snap book4 $clone@snap1
log_must eval "zfs send --redact book4 $sendfs@snap >$stream"
log_must eval "zfs recv $recvfs <$stream"
compare_files $sendfs $recvfs "f3" "$RANGE7"
log_must zfs rollback -R $clone@snap
log_must zfs destroy -R $recvfs

# Create a hole in a L0 metadata block by removing files.
log_must rm $send_mnt/manyrm_clone/f{32..96}
log_must zfs snapshot $sendfs/manyrm_clone@snap1

log_must zfs redact $sendfs/manyrm@snap book6 $sendfs/manyrm_clone@snap1
log_must eval "zfs send --redact book6 $sendfs/manyrm@snap >$stream"
log_must eval "zfs recv $recvfs <$stream"
log_must mount_redacted -f $recvfs
for i in {1..31} {97..256}; do
	diff $send_mnt/manyrm/f$i $recv_mnt/f$i || log_fail \
	    "File f$i did not match in the send and recv datasets."
done
for i in {32..96}; do
	file_size=$(stat_size $send_mnt/manyrm/f$i)
	redacted_size=$(stat_size $recv_mnt/f$i)
	[[ $file_size -eq $redacted_size ]] || log_fail \
	    "File f$i has size $file_size and redacted size $redacted_size"
done
log_must zfs rollback -R $clone@snap
log_must zfs destroy -R $recvfs

log_pass "Redacted send streams reliably handle holes."
