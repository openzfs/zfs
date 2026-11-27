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
# The dirsync and sync mount options make namespace operations commit
# the ZIL, whatever the value of the dirsync property.
#
# STRATEGY:
# 1. Mount a legacy file system with -o dirsync, then with -o sync, and
#    verify that namespace operations commit the ZIL.
# 2. Mount it without them and verify that they do not.
# 3. Do the same with zfs mount -o dirsync.
#

verify_runnable "global"

LEGACY=$TESTPOOL/$TESTFS/legacy
LEGACY_MNT=$TESTDIR/legacy_mnt
ZMOUNT=$TESTPOOL/$TESTFS/zmount

function cleanup
{
	ismounted $LEGACY && log_must umount $LEGACY_MNT
	datasetexists $LEGACY && destroy_dataset $LEGACY
	datasetexists $ZMOUNT && destroy_dataset $ZMOUNT
	[[ -d $LEGACY_MNT ]] && rmdir $LEGACY_MNT
}

log_assert "The dirsync and sync mount options commit namespace operations"
log_onexit cleanup

log_must zfs create -o mountpoint=legacy $LEGACY
log_must mkdir $LEGACY_MNT
for opt in dirsync sync; do
	log_must mount -t zfs -o $opt $LEGACY $LEGACY_MNT
	dirsync_ops $LEGACY $LEGACY_MNT yes
	log_must umount $LEGACY_MNT
done
log_must mount -t zfs $LEGACY $LEGACY_MNT
dirsync_ops $LEGACY $LEGACY_MNT no
log_must umount $LEGACY_MNT

log_must zfs create $ZMOUNT
log_must zfs unmount $ZMOUNT
log_must zfs mount -o dirsync $ZMOUNT
dirsync_ops $ZMOUNT $(get_prop mountpoint $ZMOUNT) yes
log_must zfs unmount $ZMOUNT
log_must zfs mount $ZMOUNT
dirsync_ops $ZMOUNT $(get_prop mountpoint $ZMOUNT) no

log_pass "The dirsync and sync mount options commit namespace operations"
