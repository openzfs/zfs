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
# Verify that a decompressed stream produces identical filesystem contents
# when received.
#
# Strategy:
# 1. Receive the original decompress.zsend into a test pool and hash files
# 2. Receive the decompressed stream (with records 2,0 3,0
#      and 128,131072 decompressed)
# 3. Verify file hashes are identical
#

verify_runnable "both"

log_assert "Verify decompressed stream receives with identical file contents."
log_onexit cleanup_pool $POOL

typeset src="$ZSTREAM_DATADIR/decompress.zsend.bz2"
typeset orig="$BACKDIR/decompress.orig.zsend"
typeset decompressed="$BACKDIR/decompress.out.zsend"

typeset -a records=(2,0 3,0 128,131072)

# Prepare streams
bzcat "$src" > "$orig"
log_must eval "zstream decompress ${records[*]} < '$orig' > '$decompressed'"

# Receive original and hash
recv_and_hash "$BACKDIR/hash-orig.txt" "$orig" cleanup

# Receive decompressed and hash
recv_and_hash "$BACKDIR/hash-decomp.txt" "$decompressed" cleanup

# Compare
log_must diff "$BACKDIR/hash-orig.txt" "$BACKDIR/hash-decomp.txt"

log_pass "Decompressed stream receives with identical file contents."
