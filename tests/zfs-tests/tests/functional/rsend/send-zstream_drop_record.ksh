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
# Copyright (c) 2026 by ConnectWise. All rights reserved.
#

. $STF_SUITE/tests/functional/rsend/rsend.kshlib
. $STF_SUITE/include/math.shlib

#
# Description:
# Verify that "zstream drop_record" can remove a record from a stream
#
# Strategy:
# 1. Create a file containing multiple records, both full size and embedded.
# 2. Send the dataset and drop some records
# 3. Verify the dropped records are no longer present
# 4. Verify that "zfs recv" can still receive the dataset.

verify_runnable "both"

log_assert "Verify zstream drop_record correctly drops records."
log_onexit cleanup_pool $POOL2

typeset sendfs=$POOL2/fs
typeset recvfs=$POOL2/fs2
typeset stream=$BACKDIR/stream
typeset filtered=$BACKDIR/filtered
typeset dump=$BACKDIR/dump

log_must zfs create -o compress=lz4 $sendfs
typeset dir=$(get_prop mountpoint $sendfs)

truncate -s 1m $dir/full_records
# Create some full size records
log_must dd if=/dev/urandom of=$dir/full_records conv=notrunc bs=128k count=2

# Create a file with an embedded record.  I don't know how to create a file
# with two embedded records.
recsize=16384
# For lz4, this method works for blocks up to 16k, but not larger
[[ $recsize -eq $((32 * 1024)) ]] && break
if is_illumos; then
	log_must mkholes -h 0:$((recsize - 8)) -d $((recsize - 8)):8 \
	    $dir/embedded_records
else
	log_must truncate -s 16384 $dir/embedded_records
	log_must dd if=/dev/urandom of=$dir/embedded_records \
	    seek=$((recsize - 8)) bs=1 count=8 conv=notrunc
fi

log_must zfs snapshot $sendfs@snap
typeset inode1=$(get_objnum $dir/full_records)
typeset inode2=$(get_objnum $dir/embedded_records)

# Verify that the requested records, and only them, were dropped
log_must eval "zfs send -ce $sendfs@snap > $stream"
log_must eval "zstream drop_record $inode1,131072 $inode2,0 < $stream > $filtered"
log_must eval "zstream dump -v < $filtered > $dump"
log_must grep -qE "^WRITE object = $inode1\>.*offset = 0" $dump
log_mustnot grep -qE "^WRITE object = $inode1\>.*offset = 131072" $dump
log_mustnot grep -qE "^WRITE_EMBEDDED object = $inode2\>.*offset = 0" $dump

# Verify that the stream can be received
log_must eval "zfs recv $recvfs < $stream"

log_pass "zstream drop_record correctly drops records."
