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

log_assert "Verify a snapdir with foreign filesystem mounted can still be detached."

if ! is_linux ; then
	log_unsupported "Snapdir detach is Linux-specific"
fi

function cleanup
{
	destroy_pool $TESTPOOL
}

log_onexit cleanup

# Create a pool and a snapshot
create_pool $TESTPOOL $DISKS
log_must zfs create -o snapdir=visible -o mountpoint=$TESTDIR/fs $TESTPOOL/$TESTFS
log_must zfs snapshot $TESTPOOL/$TESTFS@snap

# Mount something on the snapdir mountpoint
log_must mount -t tmpfs tmpfs-snapdir $TESTDIR/fs/$SNAPROOT/snap
log_must test "$(get_mount_paths tmpfs-snapdir)" == "$TESTDIR/fs/$SNAPROOT/snap"

# Destroy the snapshot, triggering unmount
log_must zfs destroy $TESTPOOL/$TESTFS@snap
log_must test "$(get_mount_paths tmpfs-snapdir)" == ""

log_pass "Snapdir with foreign filesystem mounted can be invalidated."

