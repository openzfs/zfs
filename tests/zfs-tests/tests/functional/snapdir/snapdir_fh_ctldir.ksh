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

. $STF_SUITE/tests/functional/snapdir/snapdir.kshlib
. $STF_SUITE/tests/functional/snapdir/snapdir.cfg

#
# DESCRIPTION:
#	A file handle for the .zfs control directories (as used by NFS)
#	resolves on a filesystem, and fails cleanly on a mounted snapshot,
#	which has no .zfs directory of its own.
#
# STRATEGY:
#	1. Create a filesystem and a snapshot, and automount the snapshot.
#	2. Open .zfs and .zfs/snapshot by file handle on the filesystem.
#	3. Verify the same handles are rejected on the mounted snapshot.
#

verify_runnable "both"

# ZFSCTL_INO_ROOT and ZFSCTL_INO_SNAPDIR
typeset INO_ROOT=0xFFFFFFFFFFFF
typeset INO_SNAPDIR=0xFFFFFFFFFFFD

function cleanup
{
	destroy_pool $TESTPOOL
}

log_onexit cleanup

log_assert "File handles for .zfs on a snapshot are rejected"

create_pool $TESTPOOL $DISKS
log_must zfs create -o mountpoint=$TESTDIR $TESTPOOL/$TESTFS
log_must zfs snapshot $TESTPOOL/$TESTFS@snap
log_must ls $TESTDIR/$SNAPROOT/snap/
log_must test "$(get_mount_paths $TESTPOOL/$TESTFS@snap)" == \
    "$TESTDIR/$SNAPROOT/snap"

log_must open_by_fid $TESTDIR $INO_ROOT 0
log_must open_by_fid $TESTDIR $INO_SNAPDIR 0

log_mustnot open_by_fid $TESTDIR/$SNAPROOT/snap $INO_ROOT 0
log_mustnot open_by_fid $TESTDIR/$SNAPROOT/snap $INO_SNAPDIR 0

log_pass "File handles for .zfs on a snapshot are rejected"
