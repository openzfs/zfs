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
# Verify that zstream decompress with "off" as the compression type
# changes record headers to mark them as uncompressed but leaves the
# actual data payload untouched.
#
# Strategy:
# 1. Decompress selected records with type "off"
# 2. Verify via zstream dump that selected records now show
#    compression type = 0 and logical_size equals the original
#    compressed_size (i.e., the header now claims the record is
#    uncompressed at the smaller, originally-compressed size)
#
# Note: we intentionally do not attempt to zfs receive the resulting
# stream. The data payloads are still compressed despite the header's
# claims otherwise, so the affected WRITE records are now inconsistent
# with the dnodes in the corresponding OBJECT records. zfs receive will
# fail with EINVAL.
#
# zstream decompress off is intended to correct a specific error case
# in which these header adjustments bring the WRITE records into alignment
# with their dnodes rather than disrupting that relationship.
#

verify_runnable "both"

log_assert "Verify decompress with 'off' changes headers but not data."

typeset src="$ZSTREAM_DATADIR/decompress.zsend.bz2"
typeset orig="$BACKDIR/decompress.orig"
typeset off_out="$BACKDIR/decompress-off.out"
typeset orig_dump="$BACKDIR/decompress-orig.dump"
typeset off_dump="$BACKDIR/decompress-off.dump"

typeset -a records=(2,0 3,0 128,131072)
typeset -a off_records=(2,0,off 3,0,off 128,131072,off)

bzcat "$src" > "$orig"

log_must eval "zstream decompress ${off_records[*]} < '$orig' > '$off_out'"

# Dump both streams
log_must eval "zstream dump -v < '$orig' > '$orig_dump' 2>&1"
log_must eval "zstream dump -v < '$off_out' > '$off_dump' 2>&1"

# Verify selected records show compression type = 0 with same logical_size
typeset failed=""
for rec in "${records[@]}"; do
	typeset obj=${rec%,*}
	typeset off=${rec#*,}

	typeset orig_line=$(awk \
	    "/^WRITE object = $obj .* offset = $off /" \
	    "$orig_dump")
	typeset orig_lsize=$(echo "$orig_line" | \
	    sed 's/.*logical_size = \([0-9]*\).*/\1/')
	typeset orig_csize=$(echo "$orig_line" | \
	    sed 's/.*compressed_size = \([0-9]*\).*/\1/')
	typeset orig_ctype=$(echo "$orig_line" | \
	    sed 's/.*compression type = \([0-9]*\).*/\1/')

	typeset off_line=$(awk \
	    "/^WRITE object = $obj .* offset = $off /" \
	    "$off_dump")
	typeset off_lsize=$(echo "$off_line" | \
	    sed 's/.*logical_size = \([0-9]*\).*/\1/')
	typeset off_csize=$(echo "$off_line" | \
	    sed 's/.*compressed_size = \([0-9]*\).*/\1/')
	typeset off_ctype=$(echo "$off_line" | \
	    sed 's/.*compression type = \([0-9]*\).*/\1/')

	if [[ "$orig_ctype" == "0" ]]; then
		log_note "Record $rec: original compression type is 0"
		failed="$failed ${rec}(orig ctype)"
	fi
	if [[ "$off_ctype" != "0" ]]; then
		log_note "Record $rec: modified compression type is not 0"
		failed="$failed ${rec}(modified ctype)"
	fi
	if [[ "$off_lsize" != "$orig_csize" ]]; then
		log_note "Record $rec: modified logical_size != original " \
		    "compressed_size ($orig_lsize -> $off_lsize)"
		failed="$failed ${rec}(lsize)"
	fi
	if [[ "$off_csize" != "0" ]]; then
		log_note "Record $rec: modified compressed_size != 0 " \
		    "($orig_csize -> $off_csize)"
		failed="$failed ${rec}(csize)"
	fi
done

[[ -z $failed ]] || \
    log_fail "Header verification failed for:$failed"

log_pass "Decompress with 'off' changes headers correctly."
