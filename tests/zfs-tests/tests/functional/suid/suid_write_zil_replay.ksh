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
# https://opensource.org/license/CDDL-1.0.
#

#
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

. $STF_SUITE/tests/functional/slog/slog.kshlib

verify_runnable "global"

function cleanup_fs
{
	cleanup
}

log_assert "Verify ZIL replay results in correct SUID/SGID bits for unprivileged write to SUID/SGID files"
log_onexit cleanup_fs
log_must setup

#
# 1. Create a file system (TESTFS)
#
log_must zpool destroy "$TESTPOOL"
log_must zpool create $TESTPOOL $VDEV log mirror $LDEV
log_must zfs set compression=on $TESTPOOL
log_must zfs create -o mountpoint="$TESTDIR" $TESTPOOL/$TESTFS

# Make all the writes from suid_write_to_file.c sync
log_must zfs set sync=always "$TESTPOOL/$TESTFS"

#
# This dd command works around an issue where ZIL records aren't created
# after freezing the pool unless a ZIL header already exists. Create a file
# synchronously to force ZFS to write one out.
#
log_must dd if=/dev/zero of=$TESTDIR/sync \
    conv=fdatasync,fsync bs=1 count=1

#
# 2. Freeze TESTFS
#
log_must zpool freeze $TESTPOOL

#
# 3. Unprivileged write to a setuid file
#
log_must suid_write_to_file "NONE"      "PRECRASH"
log_must suid_write_to_file "SUID"      "PRECRASH"
log_must suid_write_to_file "SGID"      "PRECRASH"
log_must suid_write_to_file "SUID_SGID" "PRECRASH"

#
# 4. Unmount filesystem and export the pool
#
# At this stage TESTFS is empty again and frozen, the intent log contains
# a complete set of deltas to replay.
#
log_must zfs unmount $TESTPOOL/$TESTFS

log_note "List transactions to replay:"
log_must zdb -iv $TESTPOOL/$TESTFS

log_must zpool export $TESTPOOL

#
# 5. Remount TESTFS <which replays the intent log>
#
# Import the pool to unfreeze it and claim log blocks.  It has to be
# `zpool import -f` because we can't write a frozen pool's labels!
#
log_must zpool import -f -d $VDIR $TESTPOOL

log_must suid_write_to_file "NONE"      "REPLAY"
log_must suid_write_to_file "SUID"      "REPLAY"
log_must suid_write_to_file "SGID"      "REPLAY"
log_must suid_write_to_file "SUID_SGID" "REPLAY"

log_pass
