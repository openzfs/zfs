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
# Copyright (c) 2019 by Tomohiro Kusumi. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zfs_mount/zfs_mount.cfg

#
# DESCRIPTION:
# Verify parallel mount ordering is consistent.
#
# There was a bug in initial thread dispatching algorithm which put threads
# under race condition which resulted in undefined mount order.  The purpose
# of this test is to verify `zfs unmount -a` succeeds (not `zfs mount -a`
# succeeds, it always does) after `zfs mount -a`, which could fail if threads
# race.  See github.com/openzfs/zfs/issues/{8450,8833,8878} for details.
#
# STRATEGY:
# 1. Create pools and filesystems.
# 2. Set same mount point for >1 datasets.
# 3. Unmount all datasets.
# 4. Mount all datasets.
# 5. Unmount all datasets (verify this succeeds).
#

verify_runnable "both"

DISKDIR=$(mktemp -d)
MNTPT=$DISKDIR/zfs_mount_test_race_mntpt
DISK1="$DISKDIR/zfs_mount_test_race_disk1"
DISK2="$DISKDIR/zfs_mount_test_race_disk2"

TESTPOOL1=zfs_mount_test_race_tp1
TESTPOOL2=zfs_mount_test_race_tp2

export __ZFS_POOL_RESTRICT="$TESTPOOL1 $TESTPOOL2"
log_must zfs $unmountall
unset __ZFS_POOL_RESTRICT

function cleanup
{
	zpool destroy $TESTPOOL1
	zpool destroy $TESTPOOL2
	rm -rf $DISKDIR
	rm -rf /$TESTPOOL1
	rm -rf /$TESTPOOL2
	export __ZFS_POOL_RESTRICT="$TESTPOOL1 $TESTPOOL2"
	log_must zfs $mountall
	unset __ZFS_POOL_RESTRICT
}
log_onexit cleanup

log_note "Verify parallel mount ordering is consistent"

log_must truncate -s $MINVDEVSIZE $DISK1
log_must truncate -s $MINVDEVSIZE $DISK2

log_must zpool create -f $TESTPOOL1 $DISK1
log_must zpool create -f $TESTPOOL2 $DISK2

log_must zfs create $TESTPOOL1/$TESTFS1
log_must zfs create $TESTPOOL2/$TESTFS2

log_must zfs set mountpoint=none $TESTPOOL1
log_must zfs set mountpoint=$MNTPT $TESTPOOL1/$TESTFS1

# Note that unmount can fail (due to race condition on `zfs mount -a`) with or
# without `canmount=off`.  The race has nothing to do with canmount property,
# but turn it off for convenience of mount layout used in this test case.
log_must zfs set canmount=off $TESTPOOL2
log_must zfs set mountpoint=$MNTPT $TESTPOOL2

# At this point, layout of datasets in two pools will look like below.
# Previously, on next `zfs mount -a`, pthreads assigned to TESTFS1 and TESTFS2
# could race, and TESTFS2 usually (actually always) won in OpenZFS.
# Note that the problem is how two or more threads could initially be assigned
# to the same top level directory, not this specific layout.
# This layout is just an example that can reproduce race,
# and is also the layout reported in #8833.
#
# NAME                  MOUNTED  MOUNTPOINT
# ----------------------------------------------
# /$TESTPOOL1           no       none
# /$TESTPOOL1/$TESTFS1  yes      $MNTPT
# /$TESTPOOL2           no       $MNTPT
# /$TESTPOOL2/$TESTFS2  yes      $MNTPT/$TESTFS2

# Apparently two datasets must be mounted.
log_must ismounted $TESTPOOL1/$TESTFS1
log_must ismounted $TESTPOOL2/$TESTFS2
# This unmount always succeeds, because potential race hasn't happened yet.
log_must zfs unmount -a
# This mount always succeeds, whether threads are under race condition or not.
log_must zfs mount -a

# Verify datasets are mounted (TESTFS2 fails if the race broke mount order).
log_must ismounted $TESTPOOL1/$TESTFS1
log_must ismounted $TESTPOOL2/$TESTFS2
# Verify unmount succeeds (fails if the race broke mount order).
log_must zfs unmount -a

log_pass "Verify parallel mount ordering is consistent passed"
