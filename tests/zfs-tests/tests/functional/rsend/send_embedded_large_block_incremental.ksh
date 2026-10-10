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

. $STF_SUITE/tests/functional/rsend/rsend.kshlib

#
# Description:
# Verify that a large embedded block in a -L incremental stream is received
# correctly into a dataset that previously did a non-large-block receive.
# The receiver keeps the file's smaller (128k) block size in that case, so
# the 1M DRR_WRITE_EMBEDDED record must be split across several blocks
# rather than written as a single embedded block pointer.
#
# Strategy:
# 1. Create a recordsize=1M zstd dataset with a 4M incompressible file.
# 2. Send a full stream without -L, so the received file has 128k blocks.
# 3. Overwrite the second 1M block with highly compressible data, so that
#    it is stored as an embedded block pointer.
# 4. Send an incremental with -L -c -e, verify that it carries the block
#    as a 1M WRITE_EMBEDDED record, and receive it.
# 5. Verify the received contents match and that the received file has no
#    oversized block pointer.
# 6. Verify that full and incremental sends of the received snapshot
#    succeed and receive into new datasets with identical contents.
#

verify_runnable "both"

log_assert "Large embedded blocks are received correctly into small blocks"
log_onexit cleanup_pool $POOL2

typeset sendfs=$POOL2/sendfs
typeset recvfs=$POOL2/recvfs
typeset stream=$BACKDIR/stream
typeset dump=$BACKDIR/dump
typeset zdbout=$BACKDIR/zdb.out
typeset -i recsize=$((1024 * 1024))

log_must zfs create -o recordsize=$recsize -o compression=zstd $sendfs
typeset sendfile=$(get_prop mountpoint $sendfs)/file
log_must dd if=/dev/urandom of=$sendfile bs=$recsize count=4
log_must zfs snapshot $sendfs@a

# Without -L the receiver gets 128k blocks.
log_must eval "zfs send $sendfs@a | zfs recv $recvfs"
typeset recvfile=$(get_prop mountpoint $recvfs)/file

#
# 1M of a single repeated byte compresses to well under BPE_PAYLOAD_SIZE
# (112 bytes) with zstd, so the second block becomes embedded.
#
log_must eval "tr '\0' 'a' </dev/zero | head -c $recsize | \
    dd of=$sendfile bs=$recsize seek=1 conv=notrunc"
log_must zfs snapshot $sendfs@b
sync_pool $POOL2

typeset -i obj=$(get_objnum $sendfile)
log_must eval "zdb -ddddd $sendfs $obj >$zdbout"
log_must grep -q "EMBEDDED et=0 100000L" $zdbout

log_must eval "zfs send -L -c -e -i @a $sendfs@b >$stream"
log_must eval "zstream dump -v <$stream >$dump"
log_must grep -q "WRITE_EMBEDDED object = $obj offset = $recsize \
length = $recsize" $dump

log_must eval "zfs recv $recvfs <$stream"
log_must cmp $sendfile $recvfile
log_must cmp_ds_cont $sendfs $recvfs

# The received file must not contain a block pointer larger than its blocks.
sync_pool $POOL2
log_must eval "zdb -ddddd $recvfs $obj >$zdbout"
log_mustnot grep -q "100000L" $zdbout

# The received snapshot must be sendable, in full and incrementally.
for opts in "" "-L -c -e"; do
	typeset fs=$POOL2/full${opts//[ -]/}
	log_must eval "zfs send $opts $recvfs@b | zfs recv $fs"
	log_must cmp $sendfile $(get_prop mountpoint $fs)/file
	log_must cmp_ds_cont $sendfs $fs

	fs=$POOL2/incr${opts//[ -]/}
	log_must eval "zfs send $opts $recvfs@a | zfs recv $fs"
	log_must eval "zfs send $opts -i @a $recvfs@b | zfs recv $fs"
	log_must cmp $sendfile $(get_prop mountpoint $fs)/file
	log_must cmp_ds_cont $sendfs $fs
done

log_pass "Large embedded blocks are received correctly into small blocks"
