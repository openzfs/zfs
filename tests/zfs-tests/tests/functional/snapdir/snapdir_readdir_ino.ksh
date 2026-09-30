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
# Verify that the inode numbers returned by readdir(3) (d_ino) for the
# entries of the .zfs control directory tree match the inode numbers
# returned by stat(2) for the same entries.
#
# STRATEGY:
# 1. Create a filesystem with a few snapshots.
# 2. For the dataset root, .zfs and .zfs/snapshot, compare the d_ino of
#    each directory entry with the st_ino of that entry.
# 3. Verify each .zfs/snapshot/<name> inode number is derived from the
#    snapshot objsetid, and that none of the snapshots were automounted.
# 4. Automount one snapshot and repeat the comparison; the mounted
#    snapshot root must still report the same inode number.
#

verify_runnable "both"

log_assert "Verify readdir d_ino matches stat st_ino in the .zfs directory"

if ! is_linux ; then
	log_unsupported "This test uses the Linux-only statx helper"
fi

function cleanup
{
	destroy_pool $TESTPOOL
}

log_onexit cleanup

# ZFSCTL_INO_SNAPDIRS from include/os/linux/zfs/sys/zfs_ctldir.h
# Not "typeset -i", which is limited to 32 bits in some ksh93 builds.
typeset ZFSCTL_INO_SNAPDIRS=$((16#FFFFFFFFFFFC))

#
# Print "<d_ino> <name>" for each entry in the given directory, using the
# d_ino value from readdir(3) rather than from a stat(2) of the entry.
#
function readdir_ino # <dir>
{
	python3 -c '
import os, sys
with os.scandir(sys.argv[1]) as it:
    for e in it:
        print(e.inode(), e.name)
' $1
}

#
# The statx helper opens the path with O_PATH and never triggers an
# automount, so it observes whatever the parent directory lookup returns.
#
function stat_ino # <path>
{
	typeset out
	out=$(statx ino $1) || log_fail "statx ino $1 failed: $out"
	echo ${out#ino: }
}

function check_dir # <dir>
{
	typeset dir=$1
	typeset list ino name sino
	typeset -i n=0

	list=$(readdir_ino $dir) || log_fail "readdir $dir failed"
	while read -r ino name ; do
		[[ -z "$name" ]] && continue
		sino=$(stat_ino $dir/$name)
		log_note "$dir/$name: d_ino $ino st_ino $sino"
		[[ "$ino" == "$sino" ]] || log_fail \
		    "$dir/$name: readdir d_ino $ino != stat st_ino $sino"
		n=$((n + 1))
	done <<< "$list"
	(( n > 0 )) || log_fail "$dir: no entries returned by readdir"
}

create_pool $TESTPOOL $DISKS
log_must zfs create -o snapdir=visible -o mountpoint=$TESTDIR/fs \
    $TESTPOOL/$TESTFS
log_must touch $TESTDIR/fs/file

typeset snaps="snap1 snap2 snap3"
for s in $snaps ; do
	log_must zfs snapshot $TESTPOOL/$TESTFS@$s
done

typeset snapdir=$TESTDIR/fs/$SNAPROOT

# Nothing is mounted yet.
check_dir $TESTDIR/fs
check_dir $TESTDIR/fs/.zfs
check_dir $snapdir

typeset -A snapino
typeset objsetid
for s in $snaps ; do
	objsetid=$(get_prop objsetid $TESTPOOL/$TESTFS@$s)
	snapino[$s]=$(stat_ino $snapdir/$s)
	log_must test "${snapino[$s]}" -eq $((ZFSCTL_INO_SNAPDIRS - objsetid))
	log_must test -z "$(get_mount_paths $TESTPOOL/$TESTFS@$s)"
done

# Automount one snapshot. zfs_getattr_fast() reports the mounted snapshot
# root with the same inode number as the unmounted placeholder, so readdir
# of .zfs/snapshot must still agree with stat for every entry.
log_must ls $snapdir/snap2/
log_must test "$(get_mount_paths $TESTPOOL/$TESTFS@snap2)" == "$snapdir/snap2"
log_must test "$(stat_ino $snapdir/snap2)" == "${snapino[snap2]}"
check_dir $snapdir
check_dir $TESTDIR/fs/.zfs

log_pass "readdir d_ino matches stat st_ino in the .zfs directory"
