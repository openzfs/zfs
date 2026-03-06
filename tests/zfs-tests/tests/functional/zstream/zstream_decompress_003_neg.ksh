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
# Verify that specifying the wrong compression type (lz4 for a zstd
# stream) causes decompression to fail gracefully, leaving the output
# stream identical to the input.
#
# Strategy:
# 1. Run zstream decompress with lz4 type on zstd-compressed records
# 2. Verify the output stream is byte-identical to the input
# 3. Check stderr for failure messages
#

verify_runnable "both"

log_assert "Verify wrong compression type leaves stream unchanged."

typeset src="$ZSTREAM_DATADIR/decompress.zsend.bz2"
typeset orig="$BACKDIR/decompress.orig"
typeset output="$BACKDIR/decompress-lz4.out"
typeset errfile="$BACKDIR/decompress-lz4.err"

typeset -a records=(2,0,lz4 3,0,lz4 128,131072,lz4)

bzcat "$src" > "$orig"

# Attempt to decompress zstd records as lz4 — should fail for each
zstream decompress ${records[*]} < "$orig" > "$output" 2>"$errfile"

# Output stream must be identical to input (nothing decompressed)
log_must cmp -s "$orig" "$output"

# Stderr should contain messages about the failed decompressions
typeset errcount=$(wc -l < "$errfile")
if [[ $errcount -ne 3 ]]; then
	log_fail "Did not receive 3 error messages on stderr, got $errcount"
fi
log_note "Got $errcount lines of error output (expected)"

log_pass "Wrong compression type leaves stream unchanged."
