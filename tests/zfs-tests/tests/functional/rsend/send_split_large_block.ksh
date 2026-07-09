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
# Copyright (c) 2026 by MorganaFuture. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/rsend/rsend.kshlib

#
# Description:
# A dataset with a large recordsize can hold a single-block file whose block
# size is not a power of two.  Sending it without large blocks (no -L) splits
# that block into SPA_OLD_MAXBLOCKSIZE (128K) chunks, and the final chunk is
# smaller than the block size.  The receiver must accept such a stream, for
# plain, compressed, initial and incremental sends, and reproduce the file
# exactly.  Splitting of multi-block objects and a follow-up large-block (-L)
# incremental that rewrites the object are covered too.
#
# Previously the receiver assumed a size-mismatched WRITE record was always
# larger than the block size, so this shorter trailing chunk tripped a bad
# assertion and hung the receive_writer thread on debug builds (#17829).
#

verify_runnable "both"

typeset SPLIT=131072	# SPA_OLD_MAXBLOCKSIZE, the no-large-block split size

function cleanup
{
	rm -f $BACKDIR/fs@* $BACKDIR/fsmulti@* $BACKDIR/stream*
	for ds in fs fsmulti recv recvc recvmulti; do
		datasetexists $POOL/$ds && destroy_dataset $POOL/$ds "-rR"
	done
}

#
# Extract the logical_size of every WRITE record in a stream, one per line.
#
function write_sizes # stream
{
	zstream dump -v "$1" | awk '/^WRITE object/ {
		for (i = 1; i <= NF; i++)
			if ($i == "logical_size")
				print $(i + 2)
	}'
}

#
# Assert the stream split a large block into full 128K chunks plus a shorter
# trailing chunk (the case that used to trip the assertion).
#
function assert_split_with_tail # stream
{
	typeset full=0 tail=0 s
	for s in $(write_sizes "$1"); do
		if [[ $s -eq $SPLIT ]]; then
			full=$((full + 1))
		elif [[ $s -gt 0 && $s -lt $SPLIT ]]; then
			tail=$((tail + 1))
		fi
	done
	log_note "$1: 128K WRITEs=$full short-tail WRITEs=$tail"
	[[ $full -ge 1 ]] || log_fail "$1: expected full 128K WRITE records"
	[[ $tail -ge 1 ]] || log_fail "$1: expected a short trailing WRITE record"
}

log_assert "A non-large-block send of a large non-power-of-2 block is received intact"
log_onexit cleanup

log_must zfs create -o recordsize=1m $POOL/fs

# 300000 bytes in a recordsize=1m dataset is stored in a single block whose
# size is rounded up to 300032 bytes (293K): larger than 128K and not a power
# of two, so a send without -L must emit a short trailing 128K-split chunk.
log_must dd if=/dev/urandom of=/$POOL/fs/file bs=1000 count=300
log_must zfs snapshot $POOL/fs@snap1

# Guard the premise: were the block ever a power of two the split would be
# even and would no longer exercise the short-tail path this test targets.
typeset dblk=$(zdb -dddd $POOL/fs 2>/dev/null | awk '/ZFS plain file/ {print $4}')
log_must test "$dblk" = "293K"

# Initial send without -L: verify via zstream dump that the block really was
# split into 128K chunks with a shorter trailing chunk, then that the receive
# reproduces the file byte-for-byte.
log_must eval "zfs send $POOL/fs@snap1 >$BACKDIR/stream.snap1"
assert_split_with_tail "$BACKDIR/stream.snap1"
log_must eval "zfs recv $POOL/recv <$BACKDIR/stream.snap1"
log_must diff /$POOL/fs/file /$POOL/recv/file

# Same, but with a compressed stream.
log_must eval "zfs send -c $POOL/fs@snap1 >$BACKDIR/stream.snap1c"
assert_split_with_tail "$BACKDIR/stream.snap1c"
log_must eval "zfs recv $POOL/recvc <$BACKDIR/stream.snap1c"
log_must diff /$POOL/fs/file /$POOL/recvc/file

# Incremental send without -L: rewrite part of the split block and send the
# delta, both plain and compressed, on top of the initial receives.
log_must dd if=/dev/urandom of=/$POOL/fs/file bs=1000 count=100 seek=100 conv=notrunc
log_must zfs snapshot $POOL/fs@snap2
log_must eval "zfs send -i @snap1 $POOL/fs@snap2 >$BACKDIR/stream.snap2"
log_must eval "zfs recv -F $POOL/recv <$BACKDIR/stream.snap2"
log_must diff /$POOL/fs/file /$POOL/recv/file

log_must eval "zfs send -c -i @snap1 $POOL/fs@snap2 >$BACKDIR/stream.snap2c"
log_must eval "zfs recv -F $POOL/recvc <$BACKDIR/stream.snap2c"
log_must diff /$POOL/fs/file /$POOL/recvc/file

# Follow-up incremental *with* large blocks: rewrite the whole file and send it
# as a single large block on top of the split representation already received.
# This exercises rewriting an object that was received in split form.
log_must dd if=/dev/urandom of=/$POOL/fs/file bs=1000 count=300
log_must zfs snapshot $POOL/fs@snap3
log_must eval "zfs send -L -i @snap2 $POOL/fs@snap3 >$BACKDIR/stream.snap3L"
log_must eval "zfs recv -F $POOL/recv <$BACKDIR/stream.snap3L"
log_must diff /$POOL/fs/file /$POOL/recv/file

# Multi-block object: a file spanning several 1m blocks is split into many
# 128K chunks across blocks.  Verify the batched split records are received
# and reproduce the file exactly.
log_must zfs create -o recordsize=1m $POOL/fsmulti
log_must dd if=/dev/urandom of=/$POOL/fsmulti/file bs=1024k count=3
log_must zfs snapshot $POOL/fsmulti@snap1
log_must eval "zfs send $POOL/fsmulti@snap1 >$BACKDIR/stream.multi"
typeset nsplit=$(write_sizes "$BACKDIR/stream.multi" | grep -c "^$SPLIT$")
log_note "multi-block stream: 128K WRITEs=$nsplit"
[[ $nsplit -ge 16 ]] || log_fail "expected many 128K WRITE records for a multi-block object"
log_must eval "zfs recv $POOL/recvmulti <$BACKDIR/stream.multi"
log_must diff /$POOL/fsmulti/file /$POOL/recvmulti/file

log_pass "A non-large-block send of a large non-power-of-2 block is received intact"
