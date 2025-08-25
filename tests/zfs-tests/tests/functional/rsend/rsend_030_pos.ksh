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
# Copyright (c) 2022 by Nutanix. All rights reserved.
#

. $STF_SUITE/tests/functional/rsend/rsend.kshlib

#
# Description:
# Verify resumability of full ZFS send/receive on existing dataset
#
# Strategy:
# 1. Start a full ZFS send with redirect output to a file
# 2. Mess up the contents of the stream state file on disk
# 3. Try ZFS receive, which should fail with a checksum mismatch error
# 4. ZFS send to the stream state file again using the receive_resume_token
# 5. Verify ZFS receive without "-F" option (force recvflag) fails.
# 6. Verify ZFS receive with "-F" option completes successfully.
# 7. Repeat steps on an incremental ZFS send. It should complete
#    successfully without "-F" option.
#

verify_runnable "both"

sendfs=$POOL/sendfs
recvfs=$POOL2/recvfs
streamfs=$POOL/stream

log_assert "Verify resumability of full ZFS send/receive on existing dataset"
log_onexit resume_cleanup $sendfs $streamfs

test_fs_setup $sendfs $recvfs $streamfs

# Full send/recv on existing dataset
log_must zfs create -o readonly=on $recvfs
log_must eval "zfs send -c -v $sendfs@a >/$streamfs/1"
mess_send_file /$streamfs/1
log_mustnot eval "zfs recv -suvF $recvfs </$streamfs/1"
token=$(get_prop receive_resume_token $recvfs)
log_must eval "zfs send -t $token  >/$streamfs/2"
log_mustnot eval "zfs recv -suv $recvfs </$streamfs/2"
log_must eval "zfs recv -suvF $recvfs </$streamfs/2"
file_check $sendfs $recvfs

# Incremental send/recv
log_must eval "zfs send -c -v -i @a $sendfs@b >/$streamfs/3"
mess_send_file /$streamfs/3
log_mustnot eval "zfs recv -suvF $recvfs </$streamfs/3"
token=$(get_prop receive_resume_token $recvfs)
log_must eval "zfs send -t $token  >/$streamfs/4"
log_must eval "zfs recv -suv $recvfs </$streamfs/4"
file_check $sendfs $recvfs

log_pass "Verify resumability of full ZFS send/receive on existing dataset"
