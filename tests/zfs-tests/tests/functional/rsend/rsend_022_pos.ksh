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
# Copyright (c) 2014, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/rsend/rsend.kshlib

#
# Description:
# Verify resumability of an incremental ZFS send/receive with ZFS bookmarks in
# the presence of a corrupted stream.
#
# Strategy:
# 1. Bookmark a ZFS snapshot
# 2. Destroy the ZFS snapshot
# 3. Destroy the filesystem for the receive
# 4. Verify receive of the full send stream
# 5. Start an incremental ZFS send of the ZFS bookmark, redirect output to a
#    file
# 6. Mess up the contents of the stream state file on disk
# 7. Try ZFS receive, which should fail with a checksum mismatch error
# 8. ZFS send to the stream state file again using the receive_resume_token
# 9. ZFS receive and verify the receive completes successfully
#

verify_runnable "both"

log_assert "Verify resumability of an incremental ZFS send/receive with ZFS " \
    "bookmarks"

sendfs=$POOL/sendfs
recvfs=$POOL2/recvfs
streamfs=$POOL/stream

log_onexit resume_cleanup $sendfs $streamfs

test_fs_setup $sendfs $recvfs $streamfs
log_must zfs bookmark $sendfs@a $sendfs#bm_a
log_must_busy zfs destroy $sendfs@a
log_must zfs receive -v $recvfs </$POOL/initial.zsend
resume_test "zfs send -i \#bm_a $sendfs@b" $streamfs $recvfs
log_must_busy zfs destroy -r -f $sendfs
log_must zfs receive -v $sendfs </$POOL/initial.zsend
log_must zfs receive -v $sendfs </$POOL/incremental.zsend
file_check $sendfs $recvfs

log_pass "Verify resumability of an incremental ZFS send/receive with ZFS " \
    "bookmarks"
