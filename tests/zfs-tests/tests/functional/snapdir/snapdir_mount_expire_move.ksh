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

log_assert "Verify a moved snapshot automount survives expiry"

if ! is_linux ; then
	log_unsupported "EXPIRE_SNAPSHOT tunable and mount --move only available on Linux"
fi

save_tunable EXPIRE_SNAPSHOT

function cleanup
{
	destroy_pool $TESTPOOL
	rmdir $TESTDIR/move1
	restore_tunable EXPIRE_SNAPSHOT
}

log_onexit cleanup

# Set snapshot expiry time to something we can test sanely.
log_must set_tunable64 EXPIRE_SNAPSHOT 2

# Create a pool and a snapshot
create_pool $TESTPOOL $DISKS
log_must zfs create -o snapdir=visible -o mountpoint=$TESTDIR/fs $TESTPOOL/$TESTFS
log_must zfs snapshot $TESTPOOL/$TESTFS@snap

# Make the dataset and any submounts private. Most systems (systemd-based)
# default to shared, which prevents moving a submount.
log_must mount --make-rprivate $TESTDIR/fs

# Trigger the mount
log_must ls -l $TESTDIR/fs/$SNAPROOT/snap
log_must test "$(get_mount_paths $TESTPOOL/$TESTFS@snap)" == "$TESTDIR/fs/$SNAPROOT/snap"

# Move the mount away
log_must mkdir $TESTDIR/move1
log_must mount --move $TESTDIR/fs/$SNAPROOT/snap $TESTDIR/move1
log_must test "$(get_mount_paths $TESTPOOL/$TESTFS@snap)" == "$TESTDIR/move1"

# Wait for the expiry, then ensure the moved mount remains
log_note "sleeping until snapshot expiry time has passed"
sleep 4
log_must test "$(get_mount_paths $TESTPOOL/$TESTFS@snap)" == "$TESTDIR/move1"

# Trigger the mount again
log_must ls -l $TESTDIR/fs/$SNAPROOT/snap
log_must test "$(get_mount_paths $TESTPOOL/$TESTFS@snap)" == "$TESTDIR/fs/$SNAPROOT/snap $TESTDIR/move1"

log_pass "Snapshot automount survives expiry after being moved away."
