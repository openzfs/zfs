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
# Verify that zstream decompress actually decompresses selected WRITE
# records. The input stream contains zstd-compressed writes.
#
# Strategy:
# 1. Decompress selected records (2,0 3,0 128,131072) from the stream
# 2. Run zstream dump -v on both original and decompressed streams
# 3. Verify the selected records now show compression type = 0,
#    compressed_size = 0, and payload_size = logical_size
#

verify_runnable "both"

log_assert "Verify zstream decompress decompresses selected WRITE records."

typeset src="$ZSTREAM_DATADIR/decompress.zsend.bz2"
typeset orig="$BACKDIR/decompress.orig.zsend"
typeset decompressed="$BACKDIR/decompress.out.zsend"
typeset orig_dump="$BACKDIR/decompress.orig.dump"
typeset decomp_dump="$BACKDIR/decompress.out.dump"

# Selected records: object,offset
typeset -a records=(2,0 3,0 128,131072)

# Decompress the bz2 and run zstream decompress on selected records
bzcat "$src" > "$orig"
log_must eval "zstream decompress ${records[*]} < '$orig' > '$decompressed'"

# Dump both streams
log_must eval "zstream dump -v < '$orig' > '$orig_dump' 2>&1"
log_must eval "zstream dump -v < '$decompressed' > '$decomp_dump' 2>&1"

# For each selected record, verify it was decompressed in the output
typeset failed=""
for rec in "${records[@]}"; do
	typeset obj=${rec%,*}
	typeset off=${rec#*,}

	# Find the WRITE line for this object/offset in the original dump
	# and extract the logical_size
	typeset orig_line=$(awk \
	    "/^WRITE object = $obj .* offset = $off /" \
	    "$orig_dump")
	typeset lsize=$(echo "$orig_line" | \
	    sed 's/.*logical_size = \([0-9]*\).*/\1/')

	# Find the same record in the decompressed dump
	typeset decomp_line=$(awk \
	    "/^WRITE object = $obj .* offset = $off /" \
	    "$decomp_dump")

	# Verify compression type = 0
	if ! echo "$decomp_line" | grep -q 'compression type = 0'; then
		log_note "Record $rec: compression type not 0"
		log_note "  got: $decomp_line"
		failed="$failed ${rec}(comp)"
	fi

	# Verify compressed_size = 0 (indicates uncompressed)
	if ! echo "$decomp_line" | grep -q 'compressed_size = 0'; then
		log_note "Record $rec: compressed_size not 0"
		failed="$failed ${rec}(csize)"
	fi

	# Verify payload_size = logical_size
	typeset psize=$(echo "$decomp_line" | \
	    sed 's/.*payload_size = \([0-9]*\).*/\1/')
	if [[ "$psize" != "$lsize" ]]; then
		log_note "Record $rec: payload_size ($psize) != logical_size ($lsize)"
		failed="$failed ${rec}(psize)"
	fi
done

[[ -z $failed ]] || \
    log_fail "Decompression verification failed for:$failed"

log_pass "zstream decompress decompresses selected WRITE records."
