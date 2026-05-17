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
# Verify that zstream recompress with "off" produces a larger (uncompressed)
# stream that still receives with identical file contents.
#
# Strategy:
# 1. Receive the original stream and compute file hashes as baseline
# 2. Recompress with "off" (decompress)
# 3. Verify the output stream is larger than the original
# 4. Receive and verify file hashes match
#

verify_runnable "both"

log_assert "Verify zstream recompress with 'off' produces larger stream."
log_onexit cleanup_pool $POOL

typeset src="$ZSTREAM_DATADIR/decompress.zsend.bz2"
typeset orig="$BACKDIR/recompress.orig"
typeset uncompressed="$BACKDIR/recompress-off.out"

bzcat "$src" > "$orig"

# Baseline
recv_and_hash "$BACKDIR/hash-baseline.txt" "$orig" cleanup

# Recompress with off
log_must eval "zstream recompress off < '$orig' > '$uncompressed'"

# Verify size is larger
typeset orig_size=$(wc -c < "$orig")
typeset uncomp_size=$(wc -c < "$uncompressed")
log_note "Original size: $orig_size, uncompressed size: $uncomp_size"
[[ $uncomp_size -gt $orig_size ]] || \
    log_fail "Uncompressed stream ($uncomp_size) not larger than original ($orig_size)"

# Receive and verify
recv_and_hash "$BACKDIR/hash-off.txt" "$uncompressed" cleanup
log_must diff "$BACKDIR/hash-baseline.txt" "$BACKDIR/hash-off.txt"

log_pass "zstream recompress with 'off' produces larger stream."
