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
# Copyright (c) 2023 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/rsend/rsend.kshlib

#
# Description:
# Verify recursive incremental sends missing snapshots behave correctly.
#
# Strategy:
# 1. Create snapshots on source filesystem.
# 2. Recursively send snapshots.
# 3. Delete snapshot on source filesystem
# 4. Perform incremental recursive send.
# 5. Verify matching snapshot lists.
#

verify_runnable "both"

sendfs=$POOL/sendfs
recvfs=$POOL2/recvfs

function cleanup {
	rm $BACKDIR/stream1
	rm $BACKDIR/stream2
	zfs destroy -r $sendfs
	zfs destroy -r $recvfs
}

log_assert "Verify recursive incremental sends missing snapshots behave correctly."
log_onexit cleanup

log_must zfs create $sendfs
log_must zfs snapshot $sendfs@A
log_must zfs snapshot $sendfs@B
log_must zfs snapshot $sendfs@C
log_must eval "zfs send -Rpv $sendfs@C > $BACKDIR/stream1"
log_must eval "zfs receive -F $recvfs < $BACKDIR/stream1"
log_must zfs list $sendfs@C

log_must zfs destroy $sendfs@C
log_must zfs snapshot $sendfs@D
log_must zfs snapshot $sendfs@E
log_must eval "zfs send -Rpv -I $sendfs@A $sendfs@E > $BACKDIR/stream2"
log_must eval "zfs receive -Fv $recvfs < $BACKDIR/stream2"
log_must zfs list $sendfs@D
log_must zfs list $sendfs@E
log_mustnot zfs list $sendfs@C
log_pass "Verify recursive incremental sends missing snapshots behave correctly."
