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
# Verify large blocks and redacted send work correctly together.
#
# Strategy:
# 1. Create a dataset and clone with a 1m recordsize, modifying a few k
#    within the first 1m of a 16m file.
# 2. Verify that the whole first 1m of the file is redacted.
# 3. Receive an incremental stream from the original snap to the snap it
#    was redacted with respect to.
# 4. Verify that the received dataset matches the clone
#

typeset ds_name="largeblocks"
typeset sendfs="$POOL/$ds_name"
typeset recvfs="$POOL2/$ds_name"
typeset clone="$POOL/${ds_name}_clone"
typeset tmpdir="$(get_prop mountpoint $POOL)/tmp"
typeset stream=$(mktemp $tmpdir/stream.XXXX)
setup_dataset $ds_name '-o recsize=1m'
typeset clone_mnt="$(get_prop mountpoint $clone)"
typeset send_mnt="$(get_prop mountpoint $sendfs)"
typeset recv_mnt="/$POOL2/$ds_name"

log_onexit redacted_cleanup $sendfs $recvfs

log_must dd if=/dev/urandom of=$clone_mnt/f1 bs=32k count=3 seek=8 conv=notrunc
log_must zfs snapshot $clone@snap1

log_must zfs redact $sendfs@snap book1 $clone@snap1
log_must eval "zfs send -L --redact book1 $sendfs@snap >$stream"
log_must stream_has_features $stream redacted large_blocks
log_must eval "zfs recv $recvfs <$stream"
compare_files $sendfs $recvfs "f1" "$RANGE11"
log_must mount_redacted -f $recvfs
log_must diff $send_mnt/f2 $recv_mnt/f2
unmount_redacted $recvfs

log_must eval "zfs send -L -i $sendfs@snap $clone@snap1 >$stream"
log_must stream_has_features $stream large_blocks
log_must eval "zfs recv $recvfs/new <$stream"
log_must directory_diff $clone_mnt $recv_mnt/new

log_pass "Large blocks and redacted send work correctly together."
