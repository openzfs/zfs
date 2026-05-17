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
# Verify that zstream dump -v output is as expected for pregenerated
# same-endian, neutral (no BEGIN nvlists), and XDR-encoded test streams.
# NV_ENCODE_NATIVE-encoded BEGIN records aren't readable on opposite-endian
# systems, so for these the output of zstream dump varies according to
# host endianness.
#
# Strategy:
# 1. For each of the test streams, run zstream dump -v
# 2. Compare stdout+stderr with the corresponding reference dump
#

verify_runnable "both"

log_assert "Verify zstream dump -v output matches reference dump files."

typeset sys_endian=$(get_system_endian)

typeset -a streams=(
	decompress
	decompress-crypt
	little-endian-long-payloads
	big-endian-long-payloads
	big-endian-all-drr-types-base-XDR
	big-endian-all-drr-types-incr-XDR
	little-endian-all-drr-types-base-XDR
	little-endian-all-drr-types-incr-XDR
)

if [[ $sys_endian == "little" ]]; then
	streams+=(
		little-endian-all-drr-types-base-NATIVE
		little-endian-all-drr-types-incr-NATIVE
	)
else
	streams+=(
		big-endian-all-drr-types-base-NATIVE
		big-endian-all-drr-types-incr-NATIVE
	)
fi

typeset failed=""

for stem in "${streams[@]}"; do
	typeset abbrev=$(get_stream_abbrev "$stem")
	typeset ref_src="$ZSTREAM_DATADIR/${abbrev}-new.dump.bz2"
	typeset send_src="$ZSTREAM_DATADIR/${stem}.zsend.bz2"
	typeset ref="$BACKDIR/${abbrev}-new.dump"
	typeset out="$BACKDIR/${abbrev}-out.dump"

	bzcat "$send_src" | zstream dump -v > "$out" 2>&1
	bzcat "$ref_src" > "$ref"

	if ! diff -q "$ref" "$out" > /dev/null 2>&1; then
		log_note "MISMATCH: $stem (abbrev $abbrev)"
		log_note "$(diff "$ref" "$out")"
		failed="$failed $stem"
	fi
done

[[ -z $failed ]] || log_fail "Dump output mismatch for:$failed"

log_pass "zstream dump -v output matches reference dump files."
