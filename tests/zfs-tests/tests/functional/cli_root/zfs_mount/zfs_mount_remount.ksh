#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or https://opensource.org/licenses/CDDL-1.0.
# See the License for the specific language governing permissions
# and limitations under the License.
#
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
# CDDL HEADER END
#

#
# Copyright 2017, loli10K <ezomori.nozomu@gmail.com>. All rights reserved.
# Copyright (c) 2026, TrueNAS.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zfs_mount/zfs_mount.kshlib

#
# DESCRIPTION:
# Verify remount functionality, especially on readonly objects.
#
# STRATEGY:
# 1. Prepare a filesystem and a snapshot
# 2. Verify we can (re)mount the dataset readonly/read-write
# 3. Verify we can mount the snapshot and it's mounted readonly
# 4. Verify we can't remount it read-write
# 5. Verify we can remount a dataset readonly and unmount it with
#    encryption=on and sync=disabled (issue #7753)
# 6. Re-import the pool readonly
# 7. Verify we can't remount its filesystem read-write
#

verify_runnable "both"

function cleanup
{
	log_must_busy zpool export $TESTPOOL
	log_must zpool import $TESTPOOL
	snapexists $TESTSNAP && destroy_dataset $TESTSNAP
	[[ -d $MNTPSNAP ]] && log_must rmdir $MNTPSNAP
	return 0
}

log_assert "Verify remount functionality on both filesystem and snapshots"

log_onexit cleanup

# 1. Prepare a filesystem and a snapshot
TESTFS=$TESTPOOL/$TESTFS
TESTSNAP="$TESTFS@snap"
datasetexists $TESTFS || log_must zfs create $TESTFS
snapexists $TESTSNAP || log_must zfs snapshot $TESTSNAP
log_must zfs set readonly=off $TESTFS
MNTPFS="$(get_prop mountpoint $TESTFS)"
MNTPSNAP="$TESTDIR/zfs_snap_mount"
log_must mkdir -p $MNTPSNAP

# 2. Verify we can (re)mount the dataset readonly/read-write
mount_is_rw $MNTPFS
mount_has_rw_option $MNTPFS
log_must remount_ro $TESTFS $MNTPFS
mount_is_ro $MNTPFS
mount_has_ro_option $MNTPFS
log_must remount_rw $TESTFS $MNTPFS
mount_is_rw $MNTPFS
mount_has_rw_option $MNTPFS

if is_linux; then
	# 3. Verify we can (re)mount the snapshot readonly
	log_must mount_default $TESTSNAP $MNTPSNAP
	mount_is_ro $MNTPSNAP
	mount_has_ro_option $MNTPSNAP
	log_must remount_ro $TESTSNAP $MNTPSNAP
	mount_is_ro $MNTPSNAP
	mount_has_ro_option $MNTPSNAP
	log_must umount $MNTPSNAP
fi

# 4. Verify we can't remount a snapshot read-write
# The "mount -o rw" command will succeed but the snapshot is mounted readonly.
# The "mount -o remount,rw" command must fail with an explicit error.
log_must mount_rw $TESTSNAP $MNTPSNAP
mount_is_ro $MNTPSNAP
mount_has_ro_option $MNTPSNAP
log_mustnot remount_rw $TESTSNAP $MNTPSNAP
mount_is_ro $MNTPSNAP
mount_has_ro_option $MNTPSNAP
log_must umount $MNTPSNAP

# 5. Verify we can remount a dataset readonly and unmount it with
#    encryption=on and sync=disabled (issue #7753)
log_must eval "echo 'password' | zfs create -o sync=disabled \
    -o encryption=on -o keyformat=passphrase $TESTFS/crypt"
CRYPT_MNTPFS="$(get_prop mountpoint $TESTFS/crypt)"
mount_is_rw $CRYPT_MNTPFS
log_must remount_ro $TESTFS/crypt $CRYPT_MNTPFS
log_must umount -f $CRYPT_MNTPFS
sync_pool $TESTPOOL

# 6. Re-import the pool readonly
log_must zpool export $TESTPOOL
log_must zpool import -o readonly=on $TESTPOOL

# 7. Verify we can't remount its filesystem read-write
mount_is_ro $MNTPFS
mount_has_ro_option $MNTPFS
log_mustnot remount_rw $MNTPFS
mount_is_ro $MNTPFS
mount_has_ro_option $MNTPFS

log_pass "Both filesystem and snapshots can be remounted correctly."
