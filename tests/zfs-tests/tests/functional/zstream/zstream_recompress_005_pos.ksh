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
# Copyright (c) 2026 by Garth Snyder. All rights reserved.
#

. $STF_SUITE/tests/functional/zstream/zstream.kshlib

#
# Description:
# Verify that zstream recompress lz4 of a zstd-5-compressed input stream
# yields a stream with a nonidentical size that zfs receives with identical
# file contents.
#
# Strategy:
# 1. Receive the original stream and compute file hashes as baseline
# 2. Recompress with lz4
# 3. Verify the output stream size differs from the original
# 4. Receive and verify file hashes match
#

verify_runnable "both"

log_assert "Verify zstream recompress with lz4 preserves data."
log_onexit cleanup_pool $POOL

typeset src="$ZSTREAM_DATADIR/decompress.zsend.bz2"
typeset orig="$BACKDIR/recompress.orig"
typeset lz4_out="$BACKDIR/recompress-lz4.out"

bzcat "$src" > "$orig"

# Baseline
recv_and_hash "$BACKDIR/hash-baseline.txt" "$orig" cleanup

# Recompress with lz4
log_must eval "zstream recompress lz4 < '$orig' > '$lz4_out'"

# Verify size is different
typeset orig_size=$(wc -c < "$orig")
typeset lz4_size=$(wc -c < "$lz4_out")
log_note "Original size: $orig_size, lz4 size: $lz4_size"
[[ $lz4_size -ne $orig_size ]] || \
    log_fail "LZ4 stream size ($lz4_size) same as original ($orig_size)"

# Receive and verify
recv_and_hash "$BACKDIR/hash-lz4.txt" "$lz4_out" cleanup
log_must diff "$BACKDIR/hash-baseline.txt" "$BACKDIR/hash-lz4.txt"

log_pass "zstream recompress with lz4 preserves data."
