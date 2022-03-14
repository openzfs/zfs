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
# Copyright (c) 2020 by Datto, Inc. All rights reserved.
#

. $STF_SUITE/tests/functional/rsend/rsend.kshlib
. $STF_SUITE/include/math.shlib

#
# Description:
# Verify compression features show up in zstream dump
#
# Strategy:
# 1. Create a full compressed send stream
# 2. Verify zstream dump shows this stream has the relevant features
# 3. Verify zstream dump's accounting of logical and compressed size is correct
# 4. Verify the toname from a resume token
# 5. Verify it fails with corrupted resume token
# 6. Verify it fails with missing resume token
#

verify_runnable "both"

log_assert "Verify zstream dump correctly interprets compressed send streams."
log_onexit cleanup_pool $POOL2

typeset sendfs=$POOL2/fs
typeset streamfs=$POOL2/fs2
typeset recvfs=$POOL2/fs3

log_must zfs create -o compress=lz4 $sendfs
log_must zfs create -o compress=lz4 $streamfs
typeset dir=$(get_prop mountpoint $sendfs)
write_compressible $dir 16m
log_must zfs snapshot $sendfs@full

log_must eval "zfs send -c $sendfs@full >$BACKDIR/full"
log_must stream_has_features $BACKDIR/full lz4 compressed
zstream dump -v $BACKDIR/full > $BACKDIR/dump.out

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

get_resume_token "zfs send -c $sendfs@full" $streamfs $recvfs
resume_token=$(</$streamfs/resume_token)
to_name_fs=$sendfs
log_must eval "zstream token $resume_token | grep $to_name_fs"

bad_resume_token="1-1162e8285b-100789c6360"
log_mustnot eval "zstream token $bad_resume_token 2>&1"
log_mustnot eval "zstream token 2>&1"

log_pass "zstream dump correctly interprets compressed send streams."
