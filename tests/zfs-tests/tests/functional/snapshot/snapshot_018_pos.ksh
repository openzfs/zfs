#! /bin/ksh -p
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
# Copyright 2022 iXsystems, Inc.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/snapshot/snapshot.cfg

#
# DESCRIPTION:
# Verify the functionality of snapshots_changed property
#
# STRATEGY:
# 1. Create a pool
# 2. Verify snapshots_changed property is NULL
# 3. Create a filesystem
# 4. Verify snapshots_changed property is NULL
# 5. Create snapshots for all filesystems
# 6. Verify snapshots_changed property shows correct time
# 7. Unmount all filesystems
# 8. Create a snapshot while unmounted
# 9. Verify snapshots_changed
# 10. Mount the filsystems
# 11. Verify snapshots_changed
# 12. Destroy the snapshots
# 13. Verify snapshots_changed
#

function cleanup
{
	create_pool $TESTPOOL $DISKS
}

verify_runnable "both"

log_assert "Verify snapshots_changed property"

log_onexit cleanup

snap_testpool="$TESTPOOL@v1"
snap_testfsv1="$TESTPOOL/$TESTFS@v1"
snap_testfsv2="$TESTPOOL/$TESTFS@v2"
snapdir=".zfs/snapshot"

# Create filesystems and check snapshots_changed is NULL
create_pool $TESTPOOL $DISKS
snap_changed_testpool=$(zfs get -H -o value -p snapshots_changed $TESTPOOL)
log_must eval "[[ $snap_changed_testpool == - ]]"
tpool_snapdir=$(get_prop mountpoint $TESTPOOL)/$snapdir
log_must eval "[[ $(stat_mtime $tpool_snapdir) == 0 ]]"

log_must zfs create $TESTPOOL/$TESTFS
snap_changed_testfs=$(zfs get -H -o value -p snapshots_changed $TESTPOOL/$TESTFS)
log_must eval "[[ $snap_changed_testfs == - ]]"
tfs_snapdir=$(get_prop mountpoint $TESTPOOL/$TESTFS)/$snapdir
log_must eval "[[ $(stat_mtime $tfs_snapdir) == 0 ]]"

# Create snapshots for filesystems and check snapshots_changed reports correct time
curr_time=$(date '+%s')
log_must zfs snapshot $snap_testpool
snap_changed_testpool=$(zfs get -H -o value -p snapshots_changed $TESTPOOL)
log_must eval "[[ $snap_changed_testpool -ge $curr_time ]]"
log_must eval "[[ $(stat_mtime $tpool_snapdir) ==  $snap_changed_testpool ]]"

curr_time=$(date '+%s')
log_must zfs snapshot $snap_testfsv1
snap_changed_testfs=$(zfs get -H -o value -p snapshots_changed $TESTPOOL/$TESTFS)
log_must eval "[[ $snap_changed_testfs -ge $curr_time ]]"
log_must eval "[[ $(stat_mtime $tfs_snapdir) ==  $snap_changed_testfs ]]"

# Unmount the filesystems and check snapshots_changed has correct value after unmount
log_must zfs unmount $TESTPOOL/$TESTFS
log_must eval "[[ $(zfs get -H -o value -p snapshots_changed $TESTPOOL/$TESTFS) == $snap_changed_testfs ]]"

# Create snapshot while unmounted
curr_time=$(date '+%s')
log_must zfs snapshot $snap_testfsv2
snap_changed_testfs=$(zfs get -H -o value -p snapshots_changed $TESTPOOL/$TESTFS)
log_must eval "[[ $snap_changed_testfs -ge $curr_time ]]"

log_must zfs unmount $TESTPOOL
log_must eval "[[ $(zfs get -H -o value -p snapshots_changed $TESTPOOL) == $snap_changed_testpool ]]"

# Mount back the filesystems and check snapshots_changed still has correct value
log_must zfs mount $TESTPOOL
log_must eval "[[ $(zfs get -H -o value -p snapshots_changed $TESTPOOL) == $snap_changed_testpool ]]"
log_must eval "[[ $(stat_mtime $tpool_snapdir) ==  $snap_changed_testpool ]]"

log_must zfs mount $TESTPOOL/$TESTFS
log_must eval "[[ $(zfs get -H -o value -p snapshots_changed $TESTPOOL/$TESTFS) == $snap_changed_testfs ]]"
log_must eval "[[ $(stat_mtime $tfs_snapdir) ==  $snap_changed_testfs ]]"

# Destroy the snapshots and check snapshots_changed shows correct time
curr_time=$(date '+%s')
log_must zfs destroy $snap_testfsv1
snap_changed_testfs=$(zfs get -H -o value -p snapshots_changed $TESTPOOL/$TESTFS)
log_must eval "[[ $snap_changed_testfs -ge $curr_time ]]"
log_must eval "[[ $(stat_mtime $tfs_snapdir) ==  $snap_changed_testfs ]]"

curr_time=$(date '+%s')
log_must zfs destroy $snap_testpool
snap_changed_testpool=$(zfs get -H -o value -p snapshots_changed $TESTPOOL)
log_must eval "[[ $snap_changed_testpool -ge $curr_time ]]"
log_must eval "[[ $(stat_mtime $tpool_snapdir) ==  $snap_changed_testpool ]]"

log_pass "snapshots_changed property behaves correctly"
