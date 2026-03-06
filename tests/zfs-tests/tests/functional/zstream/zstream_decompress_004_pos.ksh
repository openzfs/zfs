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
# Verify that specifying the correct compression type (zstd) produces
# the same output as omitting the type.
#
# Strategy:
# 1. Decompress records without specifying type
# 2. Decompress records specifying zstd as type
# 3. Verify both outputs are identical
#

verify_runnable "both"

log_assert "Verify explicit correct compression type matches default."

typeset src="$ZSTREAM_DATADIR/decompress.zsend.bz2"
typeset orig="$BACKDIR/decompress.orig"
typeset out_default="$BACKDIR/decompress-default.out"
typeset out_zstd="$BACKDIR/decompress-zstd.out"

typeset -a records=(2,0 3,0 128,131072)
typeset -a zstd_records=(2,0,zstd 3,0,zstd 128,131072,zstd)

bzcat "$src" > "$orig"

# Decompress without specifying type
log_must eval "zstream decompress ${records[*]} < '$orig' > '$out_default'"

# Decompress specifying zstd
log_must eval "zstream decompress ${zstd_records[*]} < '$orig' > '$out_zstd'"

# Both outputs must be identical
log_must cmp -s "$out_default" "$out_zstd"

log_pass "Explicit correct compression type matches default."
