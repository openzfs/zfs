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
# Verify that zstream dump on non-native-endian NATIVE-encoded streams
# eventually exits with code ENOTSUP (95 on Linux, 45 on FreeBSD) but
# still dumps the complete stream (minus nondecodable nvlists) and
# prints the rollup summary.
#
# Strategy:
# 1. Determine system endianness to identify non-native NATIVE streams
# 2. Run zstream dump -v on each non-native NATIVE stream
# 3. Verify exit code is 95 (ENOTSUP)
# 4. Verify that SUMMARY section is present and complete
#

verify_runnable "both"

log_assert "Non-native NATIVE-encoded streams exit with code 45 or 95."

typeset sys_endian=$(get_system_endian)

if [[ $sys_endian == "little" ]]; then
	typeset -a streams=(
		big-endian-all-drr-types-base-NATIVE
		big-endian-all-drr-types-incr-NATIVE
	)
else
	typeset -a streams=(
		little-endian-all-drr-types-base-NATIVE
		little-endian-all-drr-types-incr-NATIVE
	)
fi

typeset failed=""

for stem in "${streams[@]}"; do
	typeset out="$BACKDIR/${stem}-out.dump"
	typeset stream="$ZSTREAM_DATADIR/${stem}.zsend.bz2"

	bzcat "$stream" | zstream dump -v > "$out" 2>&1
	typeset rc=$?

	if [[ $rc -ne 45 && $rc -ne 95 ]]; then
		log_note "$stem: expected exit code 45 or 95, got $rc"
		failed="$failed ${stem}(rc=$rc)"
	fi

	# Verify the SUMMARY section is present
	if ! grep -q '^SUMMARY:' "$out"; then
		log_note "$stem: missing SUMMARY section"
		failed="$failed ${stem}(no-summary)"
	fi

	# Verify key summary lines are present
	if ! grep -q 'Total DRR_BEGIN records' "$out"; then
		log_note "$stem: missing DRR_BEGIN in summary"
		failed="$failed ${stem}(incomplete-summary)"
	fi

	if ! grep -q 'Total stream length' "$out"; then
		log_note "$stem: missing total stream length in summary"
		failed="$failed ${stem}(no-stream-length)"
	fi
done

[[ -z $failed ]] || \
    log_fail "Non-native NATIVE stream check failed:$failed"

log_pass "Non-native NATIVE-encoded streams exit with code 45 or 95."
