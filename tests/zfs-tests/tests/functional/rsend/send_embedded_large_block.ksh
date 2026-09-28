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
# Copyright (c) 2026 by ConnectWise. All rights reserved.
#

. $STF_SUITE/tests/functional/rsend/rsend.kshlib

#
# Description:
# Verify that an embedded block larger than SPA_OLD_MAXBLOCKSIZE is only sent
# as a DRR_WRITE_EMBEDDED record when the stream carries the large_blocks
# feature. Without that feature the DRR_OBJECT record clamps the object's
# block size to SPA_OLD_MAXBLOCKSIZE, so the receiver cannot represent a
# larger embedded block and rejects the record.
#
# Strategy:
# 1. Create a recordsize=1M zstd dataset holding a single 1M block which is
#    compressible enough to be stored as an embedded block pointer.
# 2. Send it with -c -e and no -L, and verify that the object has no
#    WRITE_EMBEDDED record and that its WRITE logical sizes sum to 1M.
# 3. Verify the stream is received correctly.
# 4. Send it with -c -e -L, verify the block is still embedded in that case,
#    and that this stream is received correctly too.
#

verify_runnable "both"

log_assert "Embedded blocks over 128k are only sent with the large_blocks feature"
log_onexit cleanup_pool $POOL2

typeset sendfs=$POOL2/sendfs
typeset recvfs=$POOL2/recvfs
typeset recvfs_l=$POOL2/recvfs_l
typeset stream=$BACKDIR/stream
typeset stream_l=$BACKDIR/stream_l
typeset dump=$BACKDIR/dump
typeset dump_l=$BACKDIR/dump_l
typeset zdbout=$BACKDIR/zdb.out
typeset -i recsize=$((1024 * 1024))

log_must zfs create -o recordsize=$recsize -o compression=zstd $sendfs
typeset file=$(get_prop mountpoint $sendfs)/embedded_1m

#
# 1M of a single repeated byte compresses to well under BPE_PAYLOAD_SIZE
# (112 bytes) with zstd. lz4 and gzip cannot reach that ratio at this block
# size, so the compression property matters here.
#
log_must eval "tr '\0' 'a' </dev/zero | head -c $recsize >$file"
log_must zfs snapshot $sendfs@snap
sync_pool $POOL2

# Confirm the test is really exercising a 1M embedded block pointer.
typeset -i obj=$(get_objnum $file)
log_must eval "zdb -ddddd $sendfs $obj >$zdbout"
log_must grep -q "EMBEDDED et=0 100000L" $zdbout

# Without large_blocks the block has to be split rather than embedded.
log_must eval "zfs send -c -e $sendfs@snap >$stream"
log_must eval "zstream dump -v <$stream >$dump"
log_mustnot stream_has_features $stream large_blocks

typeset bad=$(awk -v obj=$obj \
    '$1 == "WRITE_EMBEDDED" && $4 == obj { print }' $dump)
[[ -z $bad ]] || log_fail \
    "object $obj still embedded without large_blocks: $bad"

typeset -i wrote=$(awk -v obj=$obj '
    $1 == "WRITE" && $4 == obj {
        for (i = 1; i <= NF; i++)
            if ($i == "logical_size") sum += $(i + 2)
    }
    END { print sum + 0 }' $dump)
[[ $wrote -eq $recsize ]] || log_fail \
    "object $obj WRITE logical sizes sum to $wrote, expected $recsize"

log_must eval "zfs recv $recvfs <$stream"
log_must cmp_ds_cont $sendfs $recvfs

# With large_blocks the receiver can describe the block, so it stays embedded.
log_must eval "zfs send -c -e -L $sendfs@snap >$stream_l"
log_must eval "zstream dump -v <$stream_l >$dump_l"
log_must stream_has_features $stream_l large_blocks embed_data
log_must grep -q \
    "WRITE_EMBEDDED object = $obj offset = 0 length = $recsize" $dump_l

log_must eval "zfs recv $recvfs_l <$stream_l"
log_must cmp_ds_cont $sendfs $recvfs_l

log_pass "Embedded blocks over 128k are only sent with the large_blocks feature"
