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
# Copyright (c) 2026 by Matthias Goergens. All rights reserved.
#

. $STF_SUITE/tests/functional/zstream/zstream.kshlib

#
# Description:
# Verify that zstream dump rejects invalid record context and oversized BEGIN
# payloads while accepting valid compound streams.
#
# Strategy:
# 1. Generate malformed stream records in both byte orders
# 2. Check context and payload-size errors include the record offset
# 3. Check existing valid compound-stream fixtures remain accepted
#

verify_runnable "both"

log_assert "zstream rejects invalid record context and oversized payloads"

typeset streams=$BACKDIR/context-streams
typeset err=$BACKDIR/context-error
typeset generator=$ZSTREAM_DATADIR/make-invalid-context-streams.py

log_must python3 "$generator" "$streams"

for endian in big little; do
	log_mustnot eval \
	    "zstream dump -C $streams/nested-begin-$endian.zsend >$err 2>&1"
	log_must grep -q "nested DRR_BEGIN record at offset 312" "$err"

	log_mustnot eval \
	    "zstream dump -C $streams/record-after-end-$endian.zsend >$err 2>&1"
	log_must grep -q "record outside a stream at offset 624" "$err"

	log_mustnot eval \
	    "zstream dump -C $streams/extra-end-$endian.zsend >$err 2>&1"
	log_must grep -q "DRR_END record outside a stream at offset 624" "$err"

	log_mustnot eval \
	    "zstream dump -C $streams/compound-extra-end-$endian.zsend >$err 2>&1"
	log_must grep -q \
	    "record after compound stream conclusion at offset 936" "$err"

	log_mustnot eval \
	    "zstream dump -C $streams/oversized-begin-$endian.zsend >$err 2>&1"
	log_must grep -q "DRR_BEGIN payload too large at offset 0" "$err"
done

for endian in big little; do
	typeset stream="${endian}-endian-all-drr-types-incr-XDR.zsend.bz2"
	typeset compound="$ZSTREAM_DATADIR/$stream"
	log_must eval "bzcat $compound | zstream dump >/dev/null"
done

log_pass "zstream reports invalid stream context with record offsets"
