#!/bin/ksh
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
# Copyright (c) 2018 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/redacted_send/redacted.kshlib

#
# Description:
# Verify that compressed send streams are redacted correctly.
#
# Strategy:
# 1. Receive a redacted compressed send stream, verifying compression and
#    redaction.
# 2. Receive an incremental on the full receive, verifying compression and
#    redaction.
#

typeset ds_name="compressed"
typeset sendfs="$POOL/$ds_name"
typeset recvfs="$POOL2/$ds_name"
typeset clone="$POOL/${ds_name}_clone"
typeset tmpdir="$(get_prop mountpoint $POOL)/tmp"
typeset stream=$(mktemp $tmpdir/stream.XXXX)
setup_dataset $ds_name "-o compress=lz4"
typeset send_mnt="$(get_prop mountpoint $sendfs)"
typeset clone_mnt="$(get_prop mountpoint $clone)"

log_onexit redacted_cleanup $sendfs $recvfs

log_must stride_dd -i /dev/urandom -o $clone_mnt/f1 -b $((128 * 1024)) -c 4 -s 2
log_must zfs snapshot $clone@snap1
log_must rm $clone_mnt/f2
log_must zfs snapshot $clone@snap2

log_must zfs redact $sendfs@snap book1 $clone@snap1 $clone@snap2
log_must eval "zfs send -c --redact book1 $sendfs@snap >$stream"
log_must eval "zfs recv $recvfs <$stream"
log_must stream_has_features $stream compressed lz4 redacted
compare_files $sendfs $recvfs "f1" "$RANGE4"
verify_stream_size $stream $sendfs
log_must mount_redacted -f $recvfs
verify_stream_size $stream $recvfs
log_must unmount_redacted $recvfs

log_must eval "zfs send -c -i $sendfs@snap $clone@snap1 >$stream"
log_must eval "zfs recv $POOL2/inc1 <$stream"
log_must stream_has_features $stream compressed lz4
typeset mntpnt=$(get_prop mountpoint $POOL2)
log_must diff $clone_mnt/f1 $mntpnt/inc1/f1
log_must diff $send_mnt/f2 $mntpnt/inc1/f2

log_must eval "zfs send -c -i $sendfs@snap $clone@snap2 >$stream"
log_must eval "zfs recv $POOL2/inc2 <$stream"
log_must stream_has_features $stream compressed lz4
log_must diff $clone_mnt/f1 $mntpnt/inc1/f1
[[ -f $mntpnt/inc2/f2 ]] && log_fail "File f2 should not exist."

log_pass "Compressed send streams are redacted correctly."
