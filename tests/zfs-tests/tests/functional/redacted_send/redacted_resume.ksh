#!/bin/ksh

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
# Verify that resumable send works correctly with redacted streams.
#
# Strategy:
# 1. Do a full redacted resumable send.
# 2. Verify the received contents are correct.
# 3. Do an incremental redacted resumable send.
# 4. Verify the received contents are correct.
# 5. Verify that recv -A removes a partially received dataset.
#

typeset ds_name="resume"
typeset sendfs="$POOL/$ds_name"
typeset recvfs="$POOL2/$ds_name"
typeset clone="$POOL/${ds_name}_clone"
typeset clone1="$POOL/${ds_name}_clone1"
typeset tmpdir="$(get_prop mountpoint $POOL)/tmp"
typeset stream=$(mktemp $tmpdir/stream.XXXX)
setup_dataset $ds_name ''
typeset clone_mnt="$(get_prop mountpoint $clone)"
typeset send_mnt="$(get_prop mountpoint $sendfs)"
typeset recv_mnt="/$POOL2/$ds_name"

log_onexit redacted_cleanup $sendfs $recvfs

log_must stride_dd -i /dev/urandom -o $clone_mnt/f2 -b 512 -c 64 -s 512
log_must zfs snapshot $clone@snap1

# Do the full resumable send
log_must zfs redact $sendfs@snap book1 $clone@snap1
resume_test "zfs send --redact book1 $sendfs@snap" $tmpdir $recvfs
log_must mount_redacted -f $recvfs
log_must set_tunable32 ALLOW_REDACTED_DATASET_MOUNT 1
log_must diff $send_mnt/f1 $recv_mnt/f1
log_must eval "get_diff $send_mnt/f2 $recv_mnt/f2 >$tmpdir/get_diff.out"
typeset range=$(cat $tmpdir/get_diff.out)
[[ "$RANGE9" = "$range" ]] || log_fail "Unexpected range: $range"

log_must dd if=/dev/urandom of=$send_mnt/f3 bs=1024k count=3
log_must zfs snapshot $sendfs@snap2
log_must zfs clone $sendfs@snap2 $clone1
typeset clone1_mnt="$(get_prop mountpoint $clone1)"
log_must dd if=/dev/urandom of=$clone1_mnt/f3 bs=128k count=3 conv=notrunc
log_must zfs snapshot $clone1@snap

# Do the incremental resumable send
log_must zfs redact $sendfs@snap2 book2 $clone1@snap
resume_test "zfs send --redact book2 -i $sendfs#book1 $sendfs@snap2" \
    $tmpdir $recvfs
log_must diff $send_mnt/f1 $recv_mnt/f1
log_must diff $send_mnt/f2 $recv_mnt/f2
log_must eval "get_diff $send_mnt/f3 $recv_mnt/f3 >$tmpdir/get_diff.out"
range=$(cat $tmpdir/get_diff.out)
[[ "$RANGE10" = "$range" ]] || log_fail "Unexpected range: $range"

# Test recv -A works properly and verify saved sends are not allowed
log_mustnot zfs recv -A $recvfs
log_must zfs destroy -R $recvfs
log_mustnot zfs recv -A $recvfs
log_must eval "zfs send --redact book1 $sendfs@snap >$stream"
dd if=$stream bs=64k count=1 | log_mustnot zfs receive -s $recvfs
[[ "-" = $(get_prop receive_resume_token $recvfs) ]] && \
    log_fail "Receive token not found."
log_mustnot eval "zfs send --saved --redact book1 $recvfs > /dev/null"
log_must zfs recv -A $recvfs
log_must datasetnonexists $recvfs

log_pass "Resumable send works correctly with redacted streams."
