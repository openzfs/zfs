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
# Copyright (c) 2026 MorganaFuture
#

. $STF_SUITE/tests/functional/rsend/rsend.kshlib

#
# Description:
# Verify resumability of incremental send/receive when the incremental
# stream reallocates objects with a changed dnode slot count.
#
# Strategy:
# 1. Populate a dataset with 1k byte dnodes and snapshot
# 2. Remove objects, set dnodesize=legacy, and remount dataset so new
#    objects get recycled numbers at a smaller slot count
# 3. Remove objects, set dnodesize=2k, and remount dataset so new objects
#    expand over recently recycled slots
# 4. Send each stream into a corrupted state file, verify the receive
#    fails, then resume from the receive_resume_token
# 5. Verify the received snapshots match the sent ones
#

verify_runnable "both"

sendfs=$POOL/sendfs
recvfs=$POOL2/recvfs
streamfs=$POOL/stream

typeset saved_defer_batch=$(get_tunable RECV_DEFER_BATCH_SIZE)

function cleanup
{
	log_must set_tunable32 RECV_DEFER_BATCH_SIZE $saved_defer_batch
	resume_cleanup $sendfs $streamfs
}

log_assert "Verify resume of interrupted receive with changed dnode size"
log_onexit cleanup

# Shrink the defer batch so the receive flushes parked records mid-stream
# instead of only at end-of-stream or interruption.
log_must set_tunable32 RECV_DEFER_BATCH_SIZE $((1024 * 1024))

# 1. Populate a dataset with 1k byte dnodes and snapshot
log_must zfs create -o dnodesize=1k $sendfs
log_must zfs create $streamfs
log_must mk_files 200 262144 0 $sendfs
log_must zfs snapshot $sendfs@a

# 2. Remove objects, set dnodesize=legacy, and remount dataset so new
#    objects get recycled numbers at a smaller slot count
log_must eval "rm -f /$sendfs/*"
log_must zfs unmount $sendfs
log_must zfs set dnodesize=legacy $sendfs
log_must zfs mount $sendfs
log_must mk_files 200 262144 0 $sendfs
log_must zfs snapshot $sendfs@b

# 3. Remove objects, set dnodesize=2k, and remount dataset so new objects
#    expand over recently recycled slots
log_must eval "rm -f /$sendfs/*"
log_must zfs unmount $sendfs
log_must zfs set dnodesize=2k $sendfs
log_must zfs mount $sendfs
log_must mk_files 200 262144 0 $sendfs
log_must zfs snapshot $sendfs@c

# 4. Send each stream into a corrupted state file, verify the receive
#    fails, then resume from the receive_resume_token
resume_test "zfs send -v $sendfs@a" $streamfs $recvfs
resume_test "zfs send -v -i @a $sendfs@b" $streamfs $recvfs
resume_test "zfs send -v -i @b $sendfs@c" $streamfs $recvfs

# 5. Verify the received snapshots match the sent ones.  The receives
#    above use -u, so mount the destination first or the .zfs snapshot
#    directories the diffs need would not exist.
log_must zfs mount $recvfs
file_check $sendfs $recvfs
log_must directory_diff /$recvfs/.zfs/snapshot/c /$sendfs/.zfs/snapshot/c

log_pass "Resume of interrupted receive with changed dnode size works"
