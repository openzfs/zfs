#!/bin/ksh -p

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
# Copyright (c) 2022 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/rsend/rsend.kshlib
. $STF_SUITE/include/math.shlib

#
# Description:
# Verify compression features show up in zstream dump
#
# Strategy:
# 1. Create a compressed send stream
# 2. Recompress the stream with a different algorithm
# 3. Verify it can be received correctly
# 4. Verify the contents match the original filesystem
# 5. Create an uncompressed send stream
# 6. Compress the send stream
# 7. Verify that the stream is smaller when compressed
#

verify_runnable "both"

log_assert "Verify zstream recompress correctly modifies send streams."
log_onexit cleanup_pool $POOL2

typeset sendfs=$POOL2/fs
typeset recvfs=$POOL2/fs2

log_must zfs create -o compress=lz4 $sendfs
typeset dir=$(get_prop mountpoint $sendfs)
write_compressible $dir 16m
log_must zfs snapshot $sendfs@snap

log_must eval "zfs send -c $sendfs@snap | zstream recompress gzip-1 | zfs recv $recvfs"
typeset recvdir=$(get_prop mountpoint $recvfs)
log_must diff -r $dir $recvdir

log_must eval "zfs send $sendfs@snap >$BACKDIR/uncompressed"
log_must zstream recompress gzip-1 <$BACKDIR/uncompressed >$BACKDIR/compressed
typeset uncomp_size=$(wc -c $BACKDIR/uncompressed | awk '{print $1}')
typeset comp_size=$(wc -c $BACKDIR/compressed | awk '{print $1}')
[[ "$uncomp_size" -gt "$comp_size" ]] || log_fail "recompressed stream was not smaller"

log_pass "zstream recompress correctly modifies send streams."
