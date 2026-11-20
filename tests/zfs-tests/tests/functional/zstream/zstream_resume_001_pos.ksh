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

. $STF_SUITE/tests/functional/zstream/zstream.kshlib

#
# Description:
# Verify that "zstream resume" can resume an interrupted receive from a
# saved copy of the original stream.
#
# Strategy:
# 1. For full, compressed, large-block, raw, and incremental streams:
# 2. Save the stream and receive a truncated copy of it with "zfs recv -s".
# 3. Use "zstream resume" to build a resumed stream from the saved stream,
#    truncate that too, and receive it to produce a second resume token.
# 4. Verify that reading the saved stream from a file and from standard
#    input produce the same resumed stream.
# 5. Receive the final resumed stream and verify the received data.
#

verify_runnable "both"

log_assert "Verify zstream resume can resume a receive from a saved stream."
log_onexit cleanup_pool $POOL

typeset sendfs=$POOL/fs
typeset encfs=$POOL/enc
typeset recvfs=$POOL/recv
typeset stream=$BACKDIR/stream
typeset partial=$BACKDIR/partial
typeset resumed=$BACKDIR/resumed
typeset resumed2=$BACKDIR/resumed2
typeset passphrase="password"

function populate # <fs>
{
	typeset dir=$(get_prop mountpoint $1)
	typeset -i i

	for ((i = 0; i < 8; i++)); do
		log_must dd if=/dev/urandom of=$dir/file$i bs=128k count=8 \
		    status=none
	done
	# A large block and a file with embedded data
	log_must dd if=/dev/urandom of=$dir/large bs=1M count=2 status=none
	log_must truncate -s 16384 $dir/embedded
	log_must dd if=/dev/urandom of=$dir/embedded seek=16376 bs=1 \
	    count=8 conv=notrunc status=none
}

#
# Interrupt a receive of the given stream, then complete it by way of two
# successive "zstream resume" operations.
#
function interrupt_and_resume # <stream>
{
	typeset stream=$1
	typeset token
	typeset -i size

	size=$(stat_size $stream)
	log_must eval "head -c $((size / 3)) $stream > $partial"
	log_mustnot eval "zfs recv -s $recvfs < $partial"
	token=$(get_prop receive_resume_token $recvfs)
	log_must test "$token" != "-"

	log_must eval "zstream resume $token $stream > $resumed"
	size=$(stat_size $resumed)
	log_must eval "head -c $((size / 2)) $resumed > $partial"
	log_mustnot eval "zfs recv -s $recvfs < $partial"
	token=$(get_prop receive_resume_token $recvfs)
	log_must test "$token" != "-"

	log_must eval "zstream resume -v $token $stream > $resumed"
	log_must eval "zstream resume $token < $stream > $resumed2"
	log_must cmp $resumed $resumed2
	log_must eval "cat $stream | zstream resume $token > $resumed2"
	log_must cmp $resumed $resumed2
	log_must eval "zfs recv -s $recvfs < $resumed"
}

function verify_received # <sendfs> <snap>
{
	typeset src=$(get_prop mountpoint $1)/.zfs/snapshot/$2
	typeset dst=$(get_prop mountpoint $recvfs)/.zfs/snapshot/$2

	log_must test "$(get_prop guid $1@$2)" = \
	    "$(get_prop guid $recvfs@$2)"
	log_must directory_diff $src $dst
}

log_must zfs create -o recordsize=1m -o compress=lz4 $sendfs
populate $sendfs
log_must zfs snapshot $sendfs@a
log_must eval "echo $passphrase | zfs create -o encryption=on" \
    "-o keyformat=passphrase -o recordsize=1m $encfs"
populate $encfs
log_must zfs snapshot $encfs@a

for flags in "" "-c" "-Lec"; do
	log_note "Resuming 'zfs send $flags' stream"
	log_must eval "zfs send $flags $sendfs@a > $stream"
	interrupt_and_resume $stream
	verify_received $sendfs a
	log_must zfs destroy -r $recvfs
done

log_note "Resuming raw stream"
log_must eval "zfs send -w $encfs@a > $stream"
interrupt_and_resume $stream
log_must eval "echo $passphrase | zfs load-key $recvfs"
log_must zfs mount $recvfs
verify_received $encfs a
log_must zfs destroy -r $recvfs

log_note "Resuming incremental stream"
log_must eval "zfs send $sendfs@a | zfs recv $recvfs"
typeset dir=$(get_prop mountpoint $sendfs)
log_must rm $dir/file1 $dir/file5
log_must dd if=/dev/urandom of=$dir/file3 bs=128k count=4 conv=notrunc \
    status=none
log_must mkdir $dir/new
log_must dd if=/dev/urandom of=$dir/new/file bs=128k count=8 status=none
log_must zfs snapshot $sendfs@b
log_must eval "zfs send -i @a $sendfs@b > $stream"
interrupt_and_resume $stream
verify_received $sendfs b

log_pass "zstream resume can resume a receive from a saved stream."
