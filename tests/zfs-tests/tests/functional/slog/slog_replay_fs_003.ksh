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
# Copyright (c) 2026 by Christopher Robert Sherman. All rights reserved.
#

. $STF_SUITE/tests/functional/slog/slog.kshlib

#
# DESCRIPTION:
#	Verify that explicitly set access and modification times are
#	restored when TX_SETATTR records are replayed.
#
# STRATEGY:
#	1. Create a file system containing one file
#	2. Freeze the pool
#	3. Create an empty file, a file with data and a directory, then
#	   set distinct access and modification times on each; set only
#	   the access time of the file created before the freeze
#	4. Record the timestamps, unmount the file system and export
#	5. Import the pool <which replays the intent log>
#	6. Compare the replayed timestamps against the recorded ones
#

verify_runnable "global"

function cleanup_fs
{
	cleanup
}

function timestamps # <directory>
{
	typeset dir=$1
	typeset object

	for object in empty payload directory existing; do
		echo "$object $(stat_atime $dir/$object) $(stat_mtime $dir/$object)"
	done
}

log_assert "Replay of intent log restores explicitly set timestamps."
log_onexit cleanup_fs
log_must setup

#
# 1. Create a file system containing one file
#
log_must zpool create $TESTPOOL $VDEV log mirror $LDEV
log_must zfs create $TESTPOOL/$TESTFS
log_must touch /$TESTPOOL/$TESTFS/existing

#
# This dd command works around an issue where ZIL records aren't created
# after freezing the pool unless a ZIL header already exists. Create a file
# synchronously to force ZFS to write one out.
#
log_must dd if=/dev/zero of=/$TESTPOOL/$TESTFS/sync \
    conv=fdatasync,fsync bs=1 count=1

#
# 2. Freeze the pool
#
log_must zpool freeze $TESTPOOL

#
# 3. Set explicit timestamps.  The new objects are replayed as TX_CREATE
#    followed by TX_SETATTR; "existing" only needs its TX_SETATTR.
#
log_must touch /$TESTPOOL/$TESTFS/empty
log_must dd if=/dev/urandom of=/$TESTPOOL/$TESTFS/payload bs=128k count=1
log_must mkdir /$TESTPOOL/$TESTFS/directory
log_must touch -t 197901010101.01 /$TESTPOOL/$TESTFS/empty
log_must touch -t 198202020202.02 /$TESTPOOL/$TESTFS/payload
log_must touch -t 198503030303.03 /$TESTPOOL/$TESTFS/directory
log_must touch -a -t 198804040404.04 /$TESTPOOL/$TESTFS/existing

#
# 4. Record the timestamps, unmount the file system and export the pool.
#    stat(1) does not update access times.
#
log_must mkdir -p $TESTDIR
timestamps /$TESTPOOL/$TESTFS > $TESTDIR/expected
log_must cat $TESTDIR/expected
log_must zfs unmount /$TESTPOOL/$TESTFS

log_note "Verify transactions to replay:"
log_must zdb -iv $TESTPOOL/$TESTFS

log_must zpool export $TESTPOOL

#
# 5. Import the pool <which replays the intent log>
#
# It has to be `zpool import -f` because we can't write a frozen pool's
# labels!
#
log_must zpool import -f -d $VDIR $TESTPOOL

#
# 6. Compare the replayed timestamps against the recorded ones
#
timestamps /$TESTPOOL/$TESTFS > $TESTDIR/replayed
log_must cat $TESTDIR/replayed
log_must diff $TESTDIR/expected $TESTDIR/replayed

log_pass "Replay of intent log restores explicitly set timestamps."
