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

log_assert "Verify that stat on a snapshot dir does not trigger automount"

if ! is_linux ; then
	# FreeBSD actually _will_ trigger automount on stat, don't try to test it
	log_unsupported "This test only knows about automount behaviour on Linux"
fi

function cleanup
{
	destroy_pool $TESTPOOL
}

log_onexit cleanup

# Create a pool and a snapshot
create_pool $TESTPOOL $DISKS
log_must zfs create -o snapdir=visible -o mountpoint=$TESTDIR $TESTPOOL/$TESTFS
log_must zfs snapshot $TESTPOOL/$TESTFS@snap

# Note that we use our own statx helper here. The traditional POSIX system
# calls (stat(2), open(2), etc) generally do not trigger automount, but the
# Linux-specific variants (statx(2), openat(2), etc) do if AT_NO_AUTOMOUNT is
# not supplied, and some userspace implementations have been seen to not add
# this flag. Relying on our own implementation ensures we get the system calls
# we asked for.

# A lookup/stat on the snapdir itself should not mount the snapshot
log_must statx ino $TESTDIR/$SNAPROOT/snap
log_must test -z "$(get_mount_paths $TESTPOOL/$TESTFS@snap)"

# A lookup "beyond" the snapdir should trigger the mount
log_must statx ino $TESTDIR/$SNAPROOT/snap/
log_must test "$(get_mount_paths $TESTPOOL/$TESTFS@snap)" == "$TESTDIR/$SNAPROOT/snap"

log_pass "Snapshot automounts is not triggered by an op on the snapdir itself."
