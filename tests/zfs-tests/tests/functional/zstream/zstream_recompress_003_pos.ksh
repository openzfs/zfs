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
# Verify that zstream recompress with zstd at level 10 produces a smaller
# stream that receives with identical file contents.
#
# Strategy:
# 1. Receive the original stream and compute file hashes as baseline
# 2. Recompress the stream with zstd-10
# 3. Verify the recompressed stream is smaller than the original
# 4. Receive the recompressed stream and verify file hashes match
#

verify_runnable "both"

log_assert "Verify zstream recompress with zstd-10 produces smaller stream."
log_onexit cleanup_pool $POOL

typeset src="$ZSTREAM_DATADIR/decompress.zsend.bz2"
typeset orig="$BACKDIR/recompress.orig"
typeset recompressed="$BACKDIR/recompress-zstd10.out"
typeset orig_hash="$BACKDIR/hash-baseline.txt"
typeset rc_hash="$BACKDIR/hash-rc.txt"

bzcat "$src" > "$orig"

# Baseline: receive original and hash
recv_and_hash "$orig_hash" "$orig" cleanup

# Recompress with zstd at level 10
log_must eval "zstream recompress -l 10 zstd \
    < '$orig' > '$recompressed'"

# Verify size is smaller
typeset orig_size=$(wc -c < "$orig")
typeset recomp_size=$(wc -c < "$recompressed")
log_note "Original size: $orig_size, recompressed size: $recomp_size"
[[ $recomp_size -lt $orig_size ]] || \
    log_fail "Recompressed stream ($recomp_size) not smaller than original ($orig_size)"

# Receive recompressed and verify
recv_and_hash "$rc_hash" "$recompressed" cleanup
log_must diff "$orig_hash" "$rc_hash"

log_pass "zstream recompress with zstd-10 produces smaller stream."
