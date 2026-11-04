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
#	A .zfs/snapshot/<name> entry that has been looked up but not mounted
#	must not outlive the snapshot it refers to. After the snapshot is
#	destroyed or renamed, the old name must return ENOENT (not succeed,
#	and not ESTALE), and a snapshot that later takes the name must be
#	reported with its own inode number.
#
# STRATEGY:
#	1. Create a snapshot, stat its snapdir without mounting it.
#	2. Destroy the snapshot; stat and ls must fail with ENOENT.
#	3. Recreate the snapshot; stat must report the new snapshot's inode.
#	4. Rename the snapshot; the old name must be ENOENT, the new name must
#	   report the right inode and be mountable.
#	5. Rename a different snapshot onto the old name; it must report the
#	   other snapshot's inode.
#

verify_runnable "both"

log_assert "Verify stale unmounted snapdir entries are not reused"

if ! is_linux ; then
	log_unsupported "This test uses the Linux-only statx helper"
fi

function cleanup
{
	destroy_pool $TESTPOOL
}

log_onexit cleanup

# Inode number of the .zfs/snapshot/<name> entry for a snapshot, which is
# ZFSCTL_INO_SNAPDIRS - objsetid.
function snap_ino # <snapshot>
{
	typeset -i objsetid=$(get_prop objsetid $1)
	echo $(( 0xFFFFFFFFFFFC - objsetid ))
}

# The statx helper opens with O_PATH and never automounts.
function check_ino # <path> <snapshot>
{
	typeset out
	out=$(statx ino $1 2>&1) || log_fail "statx ino $1 failed: $out"
	log_note "$1: $out (expected $(snap_ino $2))"
	[[ "$out" == "ino: $(snap_ino $2)" ]] ||
	    log_fail "$1: $out, expected ino: $(snap_ino $2)"
}

function check_enoent # <path>
{
	typeset out

	if out=$(statx ino $1 2>&1) ; then
		log_fail "statx $1 succeeded after snapshot removal: $out"
	fi
	[[ "$out" == *"No such file or directory"* ]] ||
	    log_fail "statx $1: expected ENOENT, got: $out"

	if out=$(ls $1/ 2>&1) ; then
		log_fail "ls $1/ succeeded after snapshot removal: $out"
	fi
	[[ "$out" == *"No such file or directory"* ]] ||
	    log_fail "ls $1/: expected ENOENT, got: $out"

	log_note "$1: ENOENT as expected"
}

create_pool $TESTPOOL $DISKS
log_must zfs create -o snapdir=visible -o mountpoint=$TESTDIR/fs \
    $TESTPOOL/$TESTFS

typeset fs=$TESTPOOL/$TESTFS
typeset snaproot=$TESTDIR/fs/$SNAPROOT

# 1. Look up the snapdir without mounting it.
log_must zfs snapshot $fs@a
check_ino $snaproot/a $fs@a
log_must test -z "$(get_mount_paths $fs@a)"

# 2. Destroy it; the cached entry must go away with it.
log_must zfs destroy $fs@a
check_enoent $snaproot/a

# 3. Recreate it; the new snapshot must be reported, not the old one.
log_must zfs snapshot $fs@a
check_ino $snaproot/a $fs@a
log_must test -z "$(get_mount_paths $fs@a)"

# 4. Rename it; old name gone, new name the same snapshot, and mountable.
log_must zfs rename $fs@a $fs@b
check_enoent $snaproot/a
check_ino $snaproot/b $fs@b
log_must ls $snaproot/b/
log_must test "$(get_mount_paths $fs@b)" == "$snaproot/b"
check_ino $snaproot/b $fs@b
log_must umount $snaproot/b

# 5. Rename a different snapshot onto a name that has a cached entry.
log_must zfs snapshot $fs@c
check_ino $snaproot/b $fs@b
check_ino $snaproot/c $fs@c
log_must zfs rename $fs@b $fs@d
log_must zfs rename $fs@c $fs@b
check_enoent $snaproot/c
check_ino $snaproot/b $fs@b
check_ino $snaproot/d $fs@d
log_must ls $snaproot/b/
log_must test "$(get_mount_paths $fs@b)" == "$snaproot/b"

log_pass "Stale unmounted snapdir entries are not reused."
