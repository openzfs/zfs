#!/bin/ksh -p
#
# CDDL HEADER START
#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy is of the CDDL is also available via the Internet
# at http://www.illumos.org/license/CDDL.
#
# CDDL HEADER END
#

#
# Copyright(c) 2018 Datto Inc.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Verify multi mount functionality
#
# STRATEGY:
# 1. Create fs
# 2. Create and hold open file in filesystem
# 3. Lazy unmount
# 4. Verify remounting fs that was lazily unmounted is possible
# 5. Verify multiple mounts of the same dataset are possible
# 6. Verify bind mount doesn't prevent rename
#

verify_runnable "both"

function cleanup
{
	ismounted $MNTPFS && log_must umount $MNTPFS
	ismounted $MNTPFS2 && log_must umount $MNTPFS2
	ismounted $MNTPFS3 && log_must umount $MNTPFS3
	ismounted $MNTPFS4 && log_must umount $MNTPFS4
	ismounted $RENAMEMNT && log_must umount $RENAMEMNT
	datasetexists $TESTDS && log_must destroy_dataset "$TESTDS" "-f"
}
log_onexit cleanup

log_assert "Verify multiple mounts into one namespace are possible"

# 1. Create fs
TESTDS="$TESTPOOL/multi-mount-test"
log_must zfs create $TESTDS

# 2. Create and hold open file in filesystem
MNTPFS="$(get_prop mountpoint $TESTDS)"
FILENAME="$MNTPFS/file"
log_must mkfile 128k $FILENAME
log_must eval "exec 9<> $FILENAME" # open file

# 3. Lazy umount
if is_freebsd; then
	# FreeBSD does not support lazy unmount
	log_must umount $MNTPFS
else
	log_must umount -l $MNTPFS
fi
if [ -f $FILENAME ]; then
	log_fail "Lazy unmount failed"
fi

# 4. Verify remounting fs that was lazily unmounted is possible
log_must zfs mount $TESTDS
if [ ! -f $FILENAME ]; then
	log_fail "Lazy remount failed"
fi
log_must eval "exec 9>&-" # close fd

# 5. Verify multiple mounts of the same dataset are possible
MNTPFS2="$MNTPFS-second"
FILENAME="$MNTPFS2/file"
log_must mkdir $MNTPFS2
log_must mount -t zfs -o zfsutil $TESTDS $MNTPFS2
if [ ! -f $FILENAME ]; then
	log_fail "First multi mount failed"
fi

MNTPFS3="$MNTPFS-third"
FILENAME="$MNTPFS3/file"
log_must mkdir $MNTPFS3
log_must mount -t zfs -o zfsutil $TESTDS $MNTPFS3
if [ ! -f $FILENAME ]; then
	log_fail "Second multi mount failed"
fi

# 6. Verify bind mount doesn't prevent rename
RENAMEFS="$TESTDS-newname"
MNTPFS4="$MNTPFS-fourth"
log_must mkdir $MNTPFS4
log_must mount --bind $MNTPFS $MNTPFS4
log_must zfs rename $TESTDS $RENAMEFS
RENAMEMNT="$(get_prop mountpoint $RENAMEFS)"
FILENAME="$RENAMEMNT/file"
if [ ! -f $FILENAME ]; then
	log_fail "Rename failed"
fi
log_must zfs rename $RENAMEFS $TESTDS

log_pass "Multiple mounts are possible"
