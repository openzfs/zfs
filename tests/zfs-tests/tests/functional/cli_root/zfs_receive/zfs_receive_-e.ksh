#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# CDDL HEADER START
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
# CDDL HEADER END
#

#
# Copyright 2018, loli10K <ezomori.nozomu@gmail.com>. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# ZFS receive '-e' option can be used to discard all but the last element of
# the sent snapshot's file system name
#
# STRATEGY:
# 1. Create a filesystem with children and snapshots
# 2. Verify 'zfs receive -e' rejects invalid options
# 3. Verify 'zfs receive -e' can receive the root dataset
# 4. Verify 'zfs receive -e' can receive a replication stream
# 5. Verify 'zfs receive -e' can receive an incremental replication stream
#

verify_runnable "both"

function cleanup
{
	destroy_pool "$poolname"
	log_must rm -f "$vdevfile"
	log_must rm -f "$streamfile"
}

log_assert "ZFS receive '-e' option can be used to discard all but the last"\
	"element of the sent snapshot's file system name"
log_onexit cleanup

poolname="$TESTPOOL-zfsrecv"
recvfs="$poolname/recv"
vdevfile="$TEST_BASE_DIR/vdevfile.$$"
streamfile="$TEST_BASE_DIR/streamfile.$$"

#
# 1. Create a filesystem with children and snapshots
# NOTE: set "mountpoint=none" just to speed up the test process
#
log_must truncate -s $MINVDEVSIZE "$vdevfile"
log_must zpool create -O mountpoint=none "$poolname" "$vdevfile"
log_must zfs create -p "$poolname/fs/a/b"
log_must zfs create "$recvfs"
log_must zfs snapshot -r "$poolname@full"
log_must zfs snapshot -r "$poolname@incr"

#
# 2. Verify 'zfs receive -e' rejects invalid options
#
log_must eval "zfs send $poolname/fs@full > $streamfile"
log_mustnot eval "zfs receive -e < $streamfile"
log_mustnot eval "zfs receive -e $recvfs@snap < $streamfile"
log_mustnot eval "zfs receive -e $recvfs/1/2/3 < $streamfile"
log_mustnot eval "zfs receive -A -e $recvfs < $streamfile"
log_mustnot eval "zfs receive -e -d $recvfs < $streamfile"

#
# 3. 'zfs receive -e' can receive the root dataset
#
recvfs_rootds="$recvfs/rootds"
log_must zfs create "$recvfs_rootds"
log_must eval "zfs send $poolname@full > $streamfile"
log_must eval "zfs receive -e $recvfs_rootds < $streamfile"
log_must datasetexists "$recvfs_rootds/$poolname"
log_must snapexists "$recvfs_rootds/$poolname@full"

#
# 4. 'zfs receive -e' can receive a replication stream
#
recvfs_fs="$recvfs/fs"
log_must zfs create "$recvfs_fs"
log_must eval "zfs send -R $poolname/fs/a@full > $streamfile"
log_must eval "zfs receive -e $recvfs_fs < $streamfile"
log_must datasetexists "$recvfs_fs/a"
log_must datasetexists "$recvfs_fs/a/b"
log_must snapexists "$recvfs_fs/a@full"
log_must snapexists "$recvfs_fs/a/b@full"

#
# 5. 'zfs receive -e' can receive an incremental replication stream
#
log_must eval "zfs send -R -i full $poolname/fs/a@incr > $streamfile"
log_must eval "zfs receive -e $recvfs_fs < $streamfile"
log_must snapexists "$recvfs_fs/a@incr"
log_must snapexists "$recvfs_fs/a/b@incr"

log_pass "ZFS receive '-e' discards all but the last element of the sent"\
	"snapshot's file system name as expected"
