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
# Copyright 2025 iXsystems, Inc.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/snapdir/snapdir.cfg

#
# DESCRIPTION:
# Verify that parallel snapshot automount operations don't cause AVL tree
# panic due to duplicate mount attempts.
#
# STRATEGY:
# 1. Create a filesystem with snapdir=visible
# 2. Create a snapshot
# 3. Trigger parallel ls operations on the snapshot directory
# 4. Verify no kernel panic occurred and snapshot is accessible
#

function cleanup
{
	destroy_pool $TESTPOOL
}

verify_runnable "both"

log_assert "Verify parallel snapshot automount doesn't cause AVL tree panic"

log_onexit cleanup

# Create pool and filesystem
create_pool $TESTPOOL $DISKS
log_must zfs create -o snapdir=visible -o mountpoint=$TESTDIR $TESTPOOL/$TESTFS

# Create a snapshot
log_must zfs snapshot $SNAPFS

# Trigger parallel automount operations to reproduce the race condition.
# Multiple concurrent ls operations will attempt to automount the same
# unmounted snapshot, which previously could cause duplicate mount helpers
# and AVL tree panic.
snapdir_path="$TESTDIR/.zfs/snapshot/$TESTSNAP"
for i in {1..100}
do
	ls $snapdir_path >/dev/null 2>&1 &
done

# Wait for all background processes to complete
wait

# Verify the snapshot is accessible and properly mounted after parallel access
log_must ls $snapdir_path

# Verify we can unmount the filesystem cleanly. This confirms no processes
# are stuck in a syscall and all automated snapshots were unmounted properly.
# If the AVL panic occurred, unmount would fail.
log_must zfs unmount $TESTPOOL/$TESTFS

log_pass "Parallel snapshot automount completed without AVL tree panic"
