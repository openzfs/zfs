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
# Copyright (c) 2024 by the OpenZFS project. All rights reserved.
#

. $STF_SUITE/tests/functional/rsend/rsend.kshlib

#
# Description:
# Verify that recompressing a send stream and then decompressing it
# with zstream produces a stream identical to the original.
#
# Strategy:
# 1. Create an filesystem with compressible data
# 2. Generate a replication send stream
# 3. Pipe the stream through zstream recompress lz4 | zstream recompress off
# 4. Verify the result is byte-identical to the original stream
#

verify_runnable "both"

log_assert "Verify zstream recompress round-trip produces identical stream."
log_onexit cleanup_pool $POOL2

typeset sendfs=$POOL2/fs

log_must zfs create $sendfs
typeset dir=$(get_prop mountpoint $sendfs)
write_compressible $dir 16m
log_must zfs snapshot $sendfs@snap

log_must eval "zfs send -R $sendfs@snap >$BACKDIR/original"
log_must eval "zstream recompress lz4 <$BACKDIR/original | \
    zstream recompress off >$BACKDIR/roundtrip"

log_must cmp $BACKDIR/original $BACKDIR/roundtrip

log_pass "zstream recompress round-trip produces identical stream."
