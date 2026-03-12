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
# Copyright (c) 2026 by Sean Eric Fagan. All rights reserved.
#

. $STF_SUITE/tests/functional/rsend/rsend.kshlib

#
# Description:
# Verify recursive incremental with -X properly excludes
# specified datasets.
#
# Strategy:
# 1. Create multiple datasets on source pool.
# 2. Create snapshots on source pool.
# 3. Recursively send snapshots excluding datasets
# 4. Confirm destination pool does not include excluded datasets.
#

verify_runnable "both"

sendfs=$POOL/sendfs
recvfs=$POOL2/recvfs

function cleanup {
	rm -f $BACKDIR/list
	rm -f $BACKDIR/stream1
	zfs destroy -rf $sendfs
	zfs destroy -rf $recvfs
}

log_assert "Verify recursive sends excluding datasets behave properly."
log_onexit cleanup

log_must zfs create $sendfs
log_must zfs create $sendfs/ds1
log_must zfs create $sendfs/ds1/sub1
log_must zfs create $sendfs/ds1/sub1/sub2
log_must zfs create $sendfs/ds2
log_must zfs create $sendfs/ds2/sub1
log_must zfs create $sendfs/ds2/sub1/sub3
log_must zfs create $recvfs

log_must zfs snapshot -r $sendfs@A

# Now we'll send $sendfs@A, but exclude ds1/sub1
log_must zfs send -R --exclude ds1/sub1 $sendfs@A > $BACKDIR/stream1
log_must zfs recv -dFu $recvfs < $BACKDIR/stream1
log_must zfs list -r $recvfs > $BACKDIR/list
lost_mustnot grep -q ds1/sub1/sub2 $BACKDIR/list
lost_mustnot grep -q ds1/sub1 $BACKDIR/list
log_must grep -q ds2/sub1 $BACKDIR/list

log_pass "Verify recursive incremental excluding datasets behave properly."
