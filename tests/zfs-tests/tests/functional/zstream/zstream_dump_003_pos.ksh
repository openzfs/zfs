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
# Verify that zstream dump -v output for all same-endian and XDR-encoded
# test streams matches that of the previous version of zstream, with the
# following exceptions:
#
# 1. Add a line that describes the nvlist packing format for BEGIN records
# 2. Include DRR_OBJECT_RANGE and DRR_REDACT records in end summary
#
# The previous version of zstream does not dump opposite-endian streams
# correctly, so these don't have a comparison basis.
#

verify_runnable "both"

log_assert "Verify old-vs-new dump diff contains only expected additions."

typeset sys_endian=$(get_system_endian)

typeset -a streams=(
	little-endian-long-payloads
	big-endian-long-payloads
	little-endian-all-drr-types-base-XDR
	little-endian-all-drr-types-incr-XDR
	big-endian-all-drr-types-base-XDR
	big-endian-all-drr-types-incr-XDR
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
	typeset send_src="$ZSTREAM_DATADIR/${stem}.zsend.bz2"
	typeset old_src="$ZSTREAM_DATADIR/${abbrev}-old.dump.bz2"
	typeset new_dump="$BACKDIR/${abbrev}-new.dump"
	typeset old_dump="$BACKDIR/${abbrev}-old.dump"
	typeset filtered="$BACKDIR/${abbrev}-filtered.dump"

	bzcat "$send_src" | zstream dump -v > "$new_dump" 2>&1
	bzcat "$old_src" > "$old_dump"

	# Remove the lines that are new additions:
	# 1. "nvlist encoding = ..." lines
	# 2. Summary lines for DRR_OBJECT_RANGE and DRR_REDACT
	grep -v '^nvlist encoding = ' "$new_dump" | \
	    grep -v 'Total DRR_OBJECT_RANGE records' | \
	    grep -v 'Total DRR_REDACT records' > "$filtered"

	if ! diff -q "$old_dump" "$filtered" > /dev/null 2>&1; then
		log_note "MISMATCH after filtering: $stem (abbrev $abbrev)"
		log_note "$(diff "$old_dump" "$filtered")"
		failed="$failed $stem"
	fi
done

[[ -z $failed ]] || \
    log_fail "Filtered new dump did not match old dump for:$failed"

log_pass "Old-vs-new dump diff contains only expected additions."
