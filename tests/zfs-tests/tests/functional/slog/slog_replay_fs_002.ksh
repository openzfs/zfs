#!/bin/ksh -p
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

#
# DESCRIPTION:
#	Verify slog replay correctly when TX_REMOVEs are followed by
#	TX_CREATEs.
#
# STRATEGY:
#	1. Create a file system (TESTFS) with a lot of files
#	2. Freeze TESTFS
#	3. Remove all files then create a lot of files
#	4. Copy TESTFS to temporary location (TESTDIR/copy)
#	5. Unmount filesystem
#	   <at this stage TESTFS is empty again and unfrozen, and the
#	   intent log contains a complete set of deltas to replay it>
#	6. Remount TESTFS <which replays the intent log>
#	7. Compare TESTFS against the TESTDIR/copy
#

verify_runnable "global"

function cleanup_fs
{
	cleanup
}

log_assert "Replay of intent log succeeds."
log_onexit cleanup_fs
log_must setup

#
# 1. Create a file system (TESTFS) with a lot of files
#
log_must zpool create $TESTPOOL $VDEV log mirror $LDEV
log_must zfs set compression=on $TESTPOOL
log_must zfs create $TESTPOOL/$TESTFS

# Prep for the test of TX_REMOVE followed by TX_CREATE
dnsize=(legacy auto 1k 2k 4k 8k 16k)
NFILES=200
log_must mkdir /$TESTPOOL/$TESTFS/dir0
log_must eval 'for i in $(seq $NFILES); do zfs set dnodesize=${dnsize[$RANDOM % ${#dnsize[@]}]} $TESTPOOL/$TESTFS; touch /$TESTPOOL/$TESTFS/dir0/file.$i; done'

#
# Reimport to reset dnode allocation pointer.
# This is to make sure we will have TX_REMOVE and TX_CREATE on same id
#
log_must zpool export $TESTPOOL
log_must zpool import -f -d $VDIR $TESTPOOL

#
# This dd command works around an issue where ZIL records aren't created
# after freezing the pool unless a ZIL header already exists. Create a file
# synchronously to force ZFS to write one out.
#
log_must dd if=/dev/zero of=/$TESTPOOL/$TESTFS/sync \
    conv=fdatasync,fsync bs=1 count=1

#
# 2. Freeze TESTFS
#
log_must zpool freeze $TESTPOOL

#
# 3. Remove all files then create a lot of files
#
# TX_REMOVE followed by TX_CREATE
log_must eval 'rm -f /$TESTPOOL/$TESTFS/dir0/*'
log_must eval 'for i in $(seq $NFILES); do zfs set dnodesize=${dnsize[$RANDOM % ${#dnsize[@]}]} $TESTPOOL/$TESTFS; touch /$TESTPOOL/$TESTFS/dir0/file.$i; done'

#
# 4. Copy TESTFS to temporary location (TESTDIR/copy)
#
log_must mkdir -p $TESTDIR
log_must rsync -aHAX /$TESTPOOL/$TESTFS/ $TESTDIR/copy

#
# 5. Unmount filesystem and export the pool
#
# At this stage TESTFS is empty again and frozen, the intent log contains
# a complete set of deltas to replay.
#
log_must zfs unmount /$TESTPOOL/$TESTFS

log_note "Verify transactions to replay:"
log_must zdb -iv $TESTPOOL/$TESTFS

log_must zpool export $TESTPOOL

#
# 6. Remount TESTFS <which replays the intent log>
#
# Import the pool to unfreeze it and claim log blocks.  It has to be
# `zpool import -f` because we can't write a frozen pool's labels!
#
log_must zpool import -f -d $VDIR $TESTPOOL

#
# 7. Compare TESTFS against the TESTDIR/copy
#
log_note "Verify current block usage:"
log_must zdb -bcv $TESTPOOL

log_note "Verify number of files"
log_must test "$(ls /$TESTPOOL/$TESTFS/dir0 | wc -l)" -eq $NFILES

log_note "Verify working set diff:"
log_must replay_directory_diff $TESTDIR/copy /$TESTPOOL/$TESTFS

log_pass "Replay of intent log succeeds."
