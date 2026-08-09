#! /bin/ksh -p
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
# Copyright 2022 iXsystems, Inc.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/snapshot/snapshot.cfg

#
# DESCRIPTION:
# Verify the functionality of snapshots_changed and snapshots_changed_nsecs properties.
#
# STRATEGY:
# 1. Create a pool
# 2. Verify snapshots_changed and snapshots_changed_nsecs properties are NULL
# 3. Create a filesystem
# 4. Verify snapshots_changed and snapshots_changed_nsecs properties are NULL
# 5. Create snapshots for all filesystems
# 6. Verify snapshots_changed property shows correct time and snapshots_changed_nsecs is a valid nanosecond value
# 7. Unmount all filesystems
# 8. Create a snapshot while unmounted
# 9. Verify snapshots_changed and snapshots_changed_nsecs
# 10. Mount the filsystems
# 11. Verify snapshots_changed and snapshots_changed_nsecs
# 12. Destroy the snapshots
# 13. Verify snapshots_changed and snapshots_changed_nsecs
#

function cleanup
{
	create_pool $TESTPOOL $DISKS
}

verify_runnable "both"

log_assert "Verify snapshots_changed and snapshots_changed_nsecs properties"

log_onexit cleanup

snap_testpool="$TESTPOOL@v1"
snap_testfsv1="$TESTPOOL/$TESTFS@v1"
snap_testfsv2="$TESTPOOL/$TESTFS@v2"
snapdir=".zfs/snapshot"

# Create filesystems and check snapshots_changed is NULL
create_pool $TESTPOOL $DISKS
snap_changed_testpool=$(zfs get -H -o value -p snapshots_changed $TESTPOOL)
snap_changed_nsecs_testpool=$(zfs get -H -o value -p snapshots_changed_nsecs $TESTPOOL)
log_must eval "[[ $snap_changed_testpool == - ]]"
log_must eval "[[ $snap_changed_nsecs_testpool == - ]]"
list_changed_testpool=$(zfs list -H -p -o snapshots_changed $TESTPOOL)
list_changed_nsecs_testpool=$(zfs list -H -p -o snapshots_changed_nsecs $TESTPOOL)
log_must eval "[[ $list_changed_testpool == - ]]"
log_must eval "[[ $list_changed_nsecs_testpool == - ]]"
tpool_snapdir=$(get_prop mountpoint $TESTPOOL)/$snapdir
log_must eval "[[ $(stat_mtime $tpool_snapdir) == 0 ]]"

log_must zfs create $TESTPOOL/$TESTFS
snap_changed_testfs=$(zfs get -H -o value -p snapshots_changed $TESTPOOL/$TESTFS)
snap_changed_nsecs_testfs=$(zfs get -H -o value -p snapshots_changed_nsecs $TESTPOOL/$TESTFS)
log_must eval "[[ $snap_changed_testfs == - ]]"
log_must eval "[[ $snap_changed_nsecs_testfs == - ]]"
list_changed_testfs=$(zfs list -H -p -o snapshots_changed $TESTPOOL/$TESTFS)
list_changed_nsecs_testfs=$(zfs list -H -p -o snapshots_changed_nsecs $TESTPOOL/$TESTFS)
log_must eval "[[ $list_changed_testfs == - ]]"
log_must eval "[[ $list_changed_nsecs_testfs == - ]]"
tfs_snapdir=$(get_prop mountpoint $TESTPOOL/$TESTFS)/$snapdir
log_must eval "[[ $(stat_mtime $tfs_snapdir) == 0 ]]"

# Create snapshots for filesystems and check snapshots_changed reports correct time
curr_time=$(date '+%s')
log_must sleep 1
log_must zfs snapshot $snap_testpool
snap_changed_testpool=$(zfs get -H -o value -p snapshots_changed $TESTPOOL)
snap_changed_nsecs_testpool=$(zfs get -H -o value -p snapshots_changed_nsecs $TESTPOOL)
log_must eval "[[ $snap_changed_testpool -ge $curr_time ]]"
log_must eval "[[ $((snap_changed_nsecs_testpool / 1000000000)) == $snap_changed_testpool ]]"
list_changed_testpool=$(zfs list -H -p -o snapshots_changed $TESTPOOL)
list_changed_nsecs_testpool=$(zfs list -H -p -o snapshots_changed_nsecs $TESTPOOL)
log_must eval "[[ $list_changed_testpool == $snap_changed_testpool ]]"
log_must eval "[[ $list_changed_nsecs_testpool == $snap_changed_nsecs_testpool ]]"
log_must eval "[[ $(stat_mtime $tpool_snapdir) ==  $snap_changed_testpool ]]"

curr_time=$(date '+%s')
log_must sleep 1
log_must zfs snapshot $snap_testfsv1
snap_changed_testfs=$(zfs get -H -o value -p snapshots_changed $TESTPOOL/$TESTFS)
snap_changed_nsecs_testfs=$(zfs get -H -o value -p snapshots_changed_nsecs $TESTPOOL/$TESTFS)
log_must eval "[[ $snap_changed_testfs -ge $curr_time ]]"
log_must eval "[[ $((snap_changed_nsecs_testfs / 1000000000)) == $snap_changed_testfs ]]"
list_changed_testfs=$(zfs list -H -p -o snapshots_changed $TESTPOOL/$TESTFS)
list_changed_nsecs_testfs=$(zfs list -H -p -o snapshots_changed_nsecs $TESTPOOL/$TESTFS)
log_must eval "[[ $list_changed_testfs == $snap_changed_testfs ]]"
log_must eval "[[ $list_changed_nsecs_testfs == $snap_changed_nsecs_testfs ]]"
log_must eval "[[ $(stat_mtime $tfs_snapdir) ==  $snap_changed_testfs ]]"

# Unmount the filesystems and check snapshots_changed has correct value after unmount
log_must zfs unmount $TESTPOOL/$TESTFS
log_must eval "[[ $(zfs get -H -o value -p snapshots_changed $TESTPOOL/$TESTFS) == $snap_changed_testfs ]]"
log_must eval "[[ $(zfs get -H -o value -p snapshots_changed_nsecs $TESTPOOL/$TESTFS) == $snap_changed_nsecs_testfs ]]"

# Create snapshot while unmounted
curr_time=$(date '+%s')
log_must sleep 1
log_must zfs snapshot $snap_testfsv2
snap_changed_testfs=$(zfs get -H -o value -p snapshots_changed $TESTPOOL/$TESTFS)
snap_changed_nsecs_testfs=$(zfs get -H -o value -p snapshots_changed_nsecs $TESTPOOL/$TESTFS)
log_must eval "[[ $snap_changed_testfs -ge $curr_time ]]"
log_must eval "[[ $((snap_changed_nsecs_testfs / 1000000000)) == $snap_changed_testfs ]]"

log_must zfs unmount $TESTPOOL
log_must eval "[[ $(zfs get -H -o value -p snapshots_changed $TESTPOOL) == $snap_changed_testpool ]]"
log_must eval "[[ $(zfs get -H -o value -p snapshots_changed_nsecs $TESTPOOL) == $snap_changed_nsecs_testpool ]]"

# Mount back the filesystems and check snapshots_changed still has correct value
log_must zfs mount $TESTPOOL
log_must eval "[[ $(zfs get -H -o value -p snapshots_changed $TESTPOOL) == $snap_changed_testpool ]]"
log_must eval "[[ $(zfs get -H -o value -p snapshots_changed_nsecs $TESTPOOL) == $snap_changed_nsecs_testpool ]]"
log_must eval "[[ $(stat_mtime $tpool_snapdir) ==  $snap_changed_testpool ]]"

log_must zfs mount $TESTPOOL/$TESTFS
log_must eval "[[ $(zfs get -H -o value -p snapshots_changed $TESTPOOL/$TESTFS) == $snap_changed_testfs ]]"
log_must eval "[[ $(zfs get -H -o value -p snapshots_changed_nsecs $TESTPOOL/$TESTFS) == $snap_changed_nsecs_testfs ]]"
log_must eval "[[ $(stat_mtime $tfs_snapdir) ==  $snap_changed_testfs ]]"

# Destroy the snapshots and check snapshots_changed shows correct time
curr_time=$(date '+%s')
log_must sleep 1
log_must zfs destroy $snap_testfsv1
snap_changed_testfs=$(zfs get -H -o value -p snapshots_changed $TESTPOOL/$TESTFS)
snap_changed_nsecs_testfs=$(zfs get -H -o value -p snapshots_changed_nsecs $TESTPOOL/$TESTFS)
log_must eval "[[ $snap_changed_testfs -ge $curr_time ]]"
log_must eval "[[ $((snap_changed_nsecs_testfs / 1000000000)) == $snap_changed_testfs ]]"
log_must eval "[[ $(stat_mtime $tfs_snapdir) ==  $snap_changed_testfs ]]"

curr_time=$(date '+%s')
log_must sleep 1
log_must zfs destroy $snap_testpool
snap_changed_testpool=$(zfs get -H -o value -p snapshots_changed $TESTPOOL)
snap_changed_nsecs_testpool=$(zfs get -H -o value -p snapshots_changed_nsecs $TESTPOOL)
log_must eval "[[ $snap_changed_testpool -ge $curr_time ]]"
log_must eval "[[ $((snap_changed_nsecs_testpool / 1000000000)) == $snap_changed_testpool ]]"
log_must eval "[[ $(stat_mtime $tpool_snapdir) ==  $snap_changed_testpool ]]"

log_pass "snapshots_changed and snapshots_changed_nsecs properties behave correctly"
