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
# Copyright (c) 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/rsend/rsend.kshlib

#
# Description:
# Verify that we can receive a compressed stream into a deduped filesystem.
#
# Strategy:
# 1. Write heavily duplicated data to a filesystem and create a compressed
#    full stream.
# 2. Verify that the stream can be received correctly into a dedup=verify
#    filesystem.
#

verify_runnable "both"

log_pass "Verify a compressed stream can be received into a deduped filesystem"
log_onexit cleanup_pool $POOL2

typeset sendfs=$POOL2/sendfs
typeset recvfs=$POOL2/recvfs
typeset stream0=$BACKDIR/stream.0
typeset stream1=$BACKDIR/stream.1
typeset inc=$BACKDIR/stream.inc

log_must zfs create -o compress=lz4 $sendfs
log_must zfs create -o compress=lz4 -o dedup=verify $recvfs
typeset dir=$(get_prop mountpoint $sendfs)
for i in {0..10}; do
    log_must file_write -o overwrite -f $dir/file.$i -d R -b 4096 -c 1000
done
log_must zfs snapshot $sendfs@snap0
log_must eval "zfs send -c $sendfs@snap0 >$stream0"

# Finally, make sure the receive works correctly.
log_must eval "zfs recv -d $recvfs <$stream0"
cmp_ds_cont $sendfs $recvfs

log_pass "The compressed stream could be received into a deduped filesystem"
