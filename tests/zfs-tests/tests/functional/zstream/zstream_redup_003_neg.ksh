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
#
# Verify that "zstream redup" rejects invalid -m and -d arguments.
#
# Strategy:
#
# 1. Memory sizes that zfs(8) would not accept as numbers, and sizes too
#    large to represent, must be rejected with an error.
#
# 2. A -d directory that does not exist, is not a directory, or is not
#    writable must be rejected at startup with an error naming it, even
#    for a stream that is too small to spill.
#
# 3. Valid arguments must still work.
#

verify_runnable "both"

function cleanup
{
	destroy_dataset $POOL/rofs "-r"
	rm -rf $BACKDIR/*
}
log_onexit cleanup

log_assert "zstream redup rejects invalid -m and -d arguments."

typeset stream=$BACKDIR/stream
typeset out=$BACKDIR/out
typeset errs=$BACKDIR/errs
log_must eval "bzcat < $ZSTREAM_DATADIR/decompress.zsend.bz2 > $stream"

for m in "" abc -1 1Q 1.5.5 16E 0x10 "1 M" 1e3; do
	log_mustnot eval "zstream redup -m '$m' $stream > $out 2> $errs"
	log_must grep -q "invalid memory size" $errs
done

log_must touch $BACKDIR/file
log_must zfs create -o readonly=on $POOL/rofs
typeset rofs=$(get_prop mountpoint $POOL/rofs)
for dir in $BACKDIR/nonexistent $BACKDIR/file $rofs; do
	log_mustnot eval "zstream redup -d $dir $stream > $out 2> $errs"
	log_must grep -q "unable to create temp file in $dir" $errs
done

log_must eval "zstream redup -m 32M -d $BACKDIR $stream > $out"
log_must eval "zstream redup -d $BACKDIR -m 48m $stream > $out"

log_pass "zstream redup rejects invalid -m and -d arguments."
