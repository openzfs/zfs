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
# Copyright (c) 2026 by iXsystems, Inc.
#

. $STF_SUITE/tests/functional/snapdir/snapdir.kshlib
. $STF_SUITE/tests/functional/snapdir/snapdir.cfg

verify_runnable "both"

log_assert "Verify .zfs/snapshot/<name> reports the snapshot creation time as" \
    "its birth time whether or not the snapshot is mounted"

if ! is_linux ; then
	log_unsupported "This test uses the Linux-only statx helper"
fi

function cleanup
{
	destroy_pool $TESTPOOL
}

log_onexit cleanup

# The statx helper opens with O_PATH and never automounts, so it can observe
# the unmounted placeholder.  It exits 2 if STATX_BTIME is not in stx_mask.
function check_btime # <path> <expected seconds> <description>
{
	typeset out btime
	out=$(statx btime $1 2>&1) || log_fail "$3: statx btime failed: $out"
	btime=${out#btime: }
	log_note "$3: btime $btime (snapshot creation $2)"
	[[ ${btime%.*} -eq $2 && ${btime#*.} -eq 0 ]] ||
	    log_fail "$3: btime $btime != snapshot creation $2"
}

# Create a pool and a filesystem
create_pool $TESTPOOL $DISKS
log_must zfs create -o snapdir=visible -o mountpoint=$TESTDIR/fs \
    $TESTPOOL/$TESTFS

# The creation property has one-second granularity; make sure the snapshot's
# differs from the filesystem root's crtime, which is what the mounted root
# used to report, so that the mounted-state check below is discriminating.
sleep 2
log_must zfs snapshot $TESTPOOL/$TESTFS@snap

typeset snap=$TESTPOOL/$TESTFS@snap
typeset snapdir=$TESTDIR/fs/$SNAPROOT/snap
typeset -i creation=$(get_prop creation $snap)
typeset fs_btime
fs_btime=$(statx btime $TESTDIR/fs) ||
    log_fail "statx btime $TESTDIR/fs failed: $fs_btime"
fs_btime=${fs_btime#btime: }
log_must test ${fs_btime%.*} -lt $creation

# 1. Unmounted placeholder: reports the snapshot creation time, and
#    statx itself must not have mounted the snapshot.
log_must test -z "$(get_mount_paths $snap)"
check_btime $snapdir $creation "unmounted"
log_must test -z "$(get_mount_paths $snap)"
typeset ino=$(statx ino $snapdir)

# 2. Mounted root: trigger the automount, same birth time, same inode
#    number (zfs_getattr_fast() remaps both for the snapshot root).
log_must ls -l $snapdir
log_must test "$(get_mount_paths $snap)" == "$snapdir"
check_btime $snapdir $creation "mounted"
log_must test "$(statx ino $snapdir)" == "$ino"

# 3. After an explicit unmount the placeholder is back.
log_must umount $snapdir
log_must test -z "$(get_mount_paths $snap)"
check_btime $snapdir $creation "unmounted again"

log_pass "Snapshot dir birth time is the snapshot creation time in both states."
