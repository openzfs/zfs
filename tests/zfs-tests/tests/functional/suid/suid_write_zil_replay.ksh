#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or https://opensource.org/licenses/CDDL-1.0.
# See the License for the specific language governing permissions
# and limitations under the License.
#
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
# CDDL HEADER END
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
