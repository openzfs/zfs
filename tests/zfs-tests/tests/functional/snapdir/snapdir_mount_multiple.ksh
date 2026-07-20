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
# Copyright (c) 2026, TrueNAS.
#

. $STF_SUITE/tests/functional/snapdir/snapdir.kshlib
. $STF_SUITE/tests/functional/snapdir/snapdir.cfg

verify_runnable "both"

log_assert "Verify that multiple mounts of same dataset have independent snapshot automounts."

function cleanup
{
	destroy_pool $TESTPOOL
	rmdir $TESTDIR/mount1 $TESTDIR/mount2 $TESTDIR/mount3
}

log_onexit cleanup

# Create a pool and a snapshot. No mountpoint
create_pool $TESTPOOL $DISKS
log_must zfs create -o snapdir=visible -o mountpoint=legacy $TESTPOOL/$TESTFS
log_must zfs snapshot $TESTPOOL/$TESTFS@snap

# Create some mountpoints and mount the dataset multiple times
log_must mkdir $TESTDIR/mount1 $TESTDIR/mount2 $TESTDIR/mount3
log_must mount -t zfs $TESTPOOL/$TESTFS $TESTDIR/mount1
log_must mount -t zfs $TESTPOOL/$TESTFS $TESTDIR/mount2
log_must mount -t zfs $TESTPOOL/$TESTFS $TESTDIR/mount3

# Trigger a mount
log_must ls -l $TESTDIR/mount1/$SNAPROOT/snap
log_must test "$(get_mount_paths $TESTPOOL/$TESTFS@snap)" == \
  "$TESTDIR/mount1/$SNAPROOT/snap"

# Trigger another mount
log_must ls -l $TESTDIR/mount2/$SNAPROOT/snap
log_must test "$(get_mount_paths $TESTPOOL/$TESTFS@snap)" == \
  "$TESTDIR/mount1/$SNAPROOT/snap $TESTDIR/mount2/$SNAPROOT/snap"

# Trigger the last mount
log_must ls -l $TESTDIR/mount3/$SNAPROOT/snap
log_must test "$(get_mount_paths $TESTPOOL/$TESTFS@snap)" == \
  "$TESTDIR/mount1/$SNAPROOT/snap $TESTDIR/mount2/$SNAPROOT/snap $TESTDIR/mount3/$SNAPROOT/snap"

# Unmount one
log_must umount $TESTDIR/mount2/$SNAPROOT/snap
log_must test "$(get_mount_paths $TESTPOOL/$TESTFS@snap)" == \
  "$TESTDIR/mount1/$SNAPROOT/snap $TESTDIR/mount3/$SNAPROOT/snap"

# Destroy the snapshot, confirm all unmounts
log_must zfs destroy $TESTPOOL/$TESTFS@snap
log_must test "$(get_mount_paths $TESTPOOL/$TESTFS@snap)" == ""

log_pass "Multiple mounts of same dataset have independent snapshot automounts."
