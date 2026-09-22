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
# Copyright (c) 2026 by Garth Snyder. All rights reserved.
#

. $STF_SUITE/tests/functional/zstream/zstream.kshlib

#
# Description:
# Verify that zstream recompress with zstd at level 10 produces a smaller
# stream that receives with identical file contents. Also verify that the
# deprecated "-l level" spelling produces results identical to the composite
# "zstd-N" spelling.
#
# Strategy:
# 1. Receive the original stream and compute file hashes as a baseline
# 2. Recompress the stream with zstd-10
# 3. Verify that the recompressed stream is smaller than the original
# 4. Verify that "-l 10 zstd" warns about deprecation but produces output
#    identical to "zstd-10"
# 5. Verify that zstd-1 and zstd-10 produce different output, checking that
#    the identicality tested in step 4 is nontrivial
# 6. Verify that "-l 1 zstd" and "zstd-1" agree as well
# 7. Verify that "-l N zstd-N" is accepted, since the two levels agree
# 8. Verify that "-l N zstd-M" and "-l N <non-zstd>" are rejected
# 9. Receive the recompressed stream and verify file hashes match
#

verify_runnable "both"

log_assert "Verify zstream recompress zstd level handling."
log_onexit cleanup_pool $POOL

typeset src="$ZSTREAM_DATADIR/decompress.zsend.bz2"
typeset orig="$BACKDIR/recompress.orig"
typeset recompressed="$BACKDIR/recompress-zstd10.out"
typeset recompressed_l10="$BACKDIR/recompress-zstd10-l.out"
typeset recompressed_1="$BACKDIR/recompress-zstd1.out"
typeset recompressed_l1="$BACKDIR/recompress-zstd1-l.out"
typeset recompressed_both="$BACKDIR/recompress-zstd10-both.out"
typeset errfile="$BACKDIR/recompress-l.err"
typeset orig_hash="$BACKDIR/hash-baseline.txt"
typeset rc_hash="$BACKDIR/hash-rc.txt"

bzcat "$src" > "$orig"

# Baseline: receive original and hash
recv_and_hash "$orig_hash" "$orig" cleanup

# Recompress with zstd at level 10
log_must eval "zstream recompress zstd-10 < '$orig' > '$recompressed'"

# Verify size is smaller
typeset orig_size=$(wc -c < "$orig")
typeset recomp_size=$(wc -c < "$recompressed")
log_note "Original size: $orig_size, recompressed size: $recomp_size"
[[ $recomp_size -lt $orig_size ]] || \
    log_fail "Recompressed stream ($recomp_size) not smaller than original ($orig_size)"

#
# The deprecated "-l level" spelling is still accepted for zstd. It must
# warn, and it must produce the same output as the composite specifier.
#
log_must eval "zstream recompress -l 10 zstd \
    < '$orig' > '$recompressed_l10' 2> '$errfile'"
log_must grep -q deprecated "$errfile"
log_must cmp "$recompressed" "$recompressed_l10"

#
# Check that a different level really does produce a different stream,
# then repeat the comparison at that level.
#
log_must eval "zstream recompress zstd-1 < '$orig' > '$recompressed_1'"
log_mustnot cmp -s "$recompressed" "$recompressed_1"

log_must eval "zstream recompress -l 1 zstd \
    < '$orig' > '$recompressed_l1' 2> '$errfile'"
log_must grep -q deprecated "$errfile"
log_must cmp "$recompressed_1" "$recompressed_l1"

#
# Naming the same level twice is redundant but not contradictory, so it is
# accepted. It still warns, because -l is still deprecated.
#
log_must eval "zstream recompress -l 10 zstd-10 \
    < '$orig' > '$recompressed_both' 2> '$errfile'"
log_must grep -q deprecated "$errfile"
log_must cmp "$recompressed" "$recompressed_both"

#
# Two different levels are a genuine conflict
#
log_mustnot_expect "conflicting compression levels" eval \
    "zstream recompress -l 10 zstd-3 < '$orig' > /dev/null"

#
# -l applies only to zstd compression
#
log_mustnot_expect "use -l only with compression type" eval \
    "zstream recompress -l 10 lz4 < '$orig' > /dev/null"

# Receive recompressed and verify
recv_and_hash "$rc_hash" "$recompressed" cleanup
log_must diff "$orig_hash" "$rc_hash"

log_pass "zstream recompress handles zstd levels consistently."
