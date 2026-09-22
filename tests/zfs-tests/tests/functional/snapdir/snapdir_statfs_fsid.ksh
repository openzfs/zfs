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
#	statfs() of a '.zfs/snapshot/<name>' entry reports the snapshot's
#	fsid whether or not the snapshot is mounted, and probing it with
#	O_PATH does not mount it.  '.zfs' and '.zfs/snapshot' report the
#	fsid of the filesystem that contains them.
#
# STRATEGY:
#	1. Create a filesystem with a snapshot; verify nothing is mounted.
#	2. Probe the entry without mounting; the fsid must differ from the
#	   filesystem's and the snapshot must still be unmounted.
#	3. Mount the snapshot by traversing it; the fsid must be unchanged.
#

verify_runnable "both"

log_assert "Verify .zfs/snapshot/<name> reports the snapshot's fsid without" \
    "being mounted"

if ! is_linux ; then
	log_unsupported "This test uses the Linux-only statfs_nomount helper"
fi

function cleanup
{
	destroy_pool $TESTPOOL
}
log_onexit cleanup

function fsid # <path>
{
	typeset out
	out=$(statfs_nomount $1 2>&1) || log_fail "statfs_nomount $1: $out"
	echo $out
}

create_pool $TESTPOOL $DISKS
log_must zfs create -o snapdir=visible -o mountpoint=$TESTDIR/fs \
    $TESTPOOL/$TESTFS
log_must zfs snapshot $TESTPOOL/$TESTFS@snap

typeset snap=$TESTPOOL/$TESTFS@snap
typeset snapdir=$TESTDIR/fs/$SNAPROOT/snap
typeset fs_fsid=$(fsid $TESTDIR/fs)

# The control directories belong to the filesystem.
log_must test "$(fsid $TESTDIR/fs/.zfs)" == "$fs_fsid"
log_must test "$(fsid $TESTDIR/fs/$SNAPROOT)" == "$fs_fsid"

# Unmounted: the entry reports the snapshot's fsid and stays unmounted.
log_must test -z "$(get_mount_paths $snap)"
typeset unmounted=$(fsid $snapdir)
log_must test -z "$(get_mount_paths $snap)"
log_must test "$unmounted" != "$fs_fsid"
log_note "unmounted fsid $unmounted (filesystem $fs_fsid)"

# Mounted: same value.
log_must ls -l $snapdir
log_must test "$(get_mount_paths $snap)" == "$snapdir"
log_must test "$(fsid $snapdir)" == "$unmounted"

log_pass "Unmounted .zfs/snapshot/<name> reports the snapshot's fsid"
