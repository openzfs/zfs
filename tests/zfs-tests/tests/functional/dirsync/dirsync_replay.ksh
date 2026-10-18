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

. $STF_SUITE/tests/functional/dirsync/dirsync.kshlib

#
# DESCRIPTION:
# With dirsync=on, namespace operations survive a crash; with
# dirsync=off, they are lost.
#
# STRATEGY:
# For dirsync=off, then dirsync=on:
# 1. Create a pool on a file vdev, and entries to remove and rename.
# 2. Write a ZIL header, which log records written after a freeze need.
# 3. Freeze the pool, so that no transaction group reaches the disk.
# 4. Run namespace operations.
# 5. Copy the vdev while the pool is still imported, which is what the
#    disk holds after a crash.  Exporting instead would commit the ZIL.
# 6. Export the pool, delete its vdev and import the copy, which replays
#    the ZIL.
# 7. Verify that the operations were replayed with dirsync=on, and were
#    not with dirsync=off.
#

verify_runnable "global"

VDIR=$TEST_BASE_DIR/dirsync_vdev
CDIR=$TEST_BASE_DIR/dirsync_copy
FS=$TESTPOOL1/fs

function cleanup
{
	poolexists $TESTPOOL1 && destroy_pool $TESTPOOL1
	rm -rf $VDIR $CDIR $TESTDIR1
}

function dirsync_replay # on|off
{
	typeset value=$1
	typeset new
	typeset old

	log_must mkdir -p $VDIR $CDIR
	log_must truncate -s $MINVDEVSIZE $VDIR/vdev
	log_must zpool create -O mountpoint=$TESTDIR1 $TESTPOOL1 $VDIR/vdev
	log_must zfs create -o dirsync=$value $FS
	typeset mnt=$(get_prop mountpoint $FS)

	log_must touch $mnt/old $mnt/moved
	log_must mkdir $mnt/olddir
	log_must dd if=/dev/zero of=$mnt/sync bs=512 count=1 conv=fsync
	log_must zpool freeze $TESTPOOL1

	typeset -i before=$(kstat_dataset $FS zil_commit_count)
	log_must touch $mnt/file
	log_must mkfifo $mnt/fifo
	log_must mkdir $mnt/dir
	log_must ln -s file $mnt/symlink
	log_must ln $mnt/old $mnt/link
	log_must mv $mnt/moved $mnt/renamed
	log_must rm $mnt/old
	log_must rmdir $mnt/olddir
	typeset -i after=$(kstat_dataset $FS zil_commit_count)
	if [[ $value == "off" ]] && (( after != before )); then
		log_fail "ZIL committed $((after - before)) times with dirsync=off"
	fi

	log_must cp $VDIR/vdev $CDIR/vdev
	log_must zpool export $TESTPOOL1
	log_must rm $VDIR/vdev
	log_must zpool import -f -d $CDIR $TESTPOOL1
	log_must ismounted $FS

	new="$mnt/file $mnt/fifo $mnt/dir $mnt/symlink $mnt/link $mnt/renamed"
	old="$mnt/old $mnt/moved $mnt/olddir"
	if [[ $value == "off" ]]; then
		typeset tmp=$new
		new=$old
		old=$tmp
	fi
	for f in $new; do
		log_must test -e $f -o -L $f
	done
	for f in $old; do
		log_mustnot test -e $f -o -L $f
	done

	cleanup
}

log_assert "Namespace operations survive a crash with dirsync=on"
log_onexit cleanup

dirsync_replay off
dirsync_replay on

log_pass "Namespace operations survive a crash with dirsync=on"
