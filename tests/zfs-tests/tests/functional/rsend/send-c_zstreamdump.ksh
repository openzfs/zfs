#!/bin/ksh -p

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
# Copyright (c) 2015 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/rsend/rsend.kshlib
. $STF_SUITE/include/math.shlib

#
# Description:
# Verify compression features show up in zstreamdump
#
# Strategy:
# 1. Create a full compressed send stream
# 2. Verify zstreamdump shows this stream has the relevant features
# 3. Verify zstreamdump's accounting of logical and compressed size is correct
#

verify_runnable "both"

log_assert "Verify zstreamdump correctly interprets compressed send streams."
log_onexit cleanup_pool $POOL2

typeset sendfs=$POOL2/fs

log_must zfs create -o compress=lz4 $sendfs
typeset dir=$(get_prop mountpoint $sendfs)
write_compressible $dir 16m
log_must zfs snapshot $sendfs@full

log_must eval "zfs send -c $sendfs@full >$BACKDIR/full"
log_must stream_has_features $BACKDIR/full lz4 compressed
cat $BACKDIR/full | zstreamdump -v > $BACKDIR/dump.out

lsize=$(awk '/^WRITE [^0]/ {lsize += $24} END {printf("%d", lsize)}' \
    $BACKDIR/dump.out)
lsize_prop=$(get_prop logicalused $sendfs)
within_percent $lsize $lsize_prop 90 || log_fail \
    "$lsize and $lsize_prop differed by too much"

csize=$(awk '/^WRITE [^0]/ {csize += $27} END {printf("%d", csize)}' \
    $BACKDIR/dump.out)
csize_prop=$(get_prop used $sendfs)
within_percent $csize $csize_prop 90 || log_fail \
    "$csize and $csize_prop differed by too much"

log_pass "zstreamdump correctly interprets compressed send streams."
