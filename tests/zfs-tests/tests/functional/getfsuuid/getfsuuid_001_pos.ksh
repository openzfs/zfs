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
# Copyright 2026 Ricardo Correia.  All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/getfsuuid/getfsuuid.kshlib

#
# DESCRIPTION:
# The FS_IOC_GETFSUUID ioctl returns a UUID that contains the pool guid
# and the dataset guid, on regular files and on directories, for
# filesystems, for snapshot automounts, for received snapshots, and for
# clones.
#
# STRATEGY:
# 1. Get the UUID of the mountpoint directory of a filesystem.
# 2. Verify that the two UUID halves equal the pool and dataset guids.
# 3. Verify that a regular file returns the same UUID.
# 4. Verify that a second filesystem returns the same pool half and its
#    own dataset guid.
# 5. Verify that the UUID stays the same after an unmount and a mount.
# 6. Verify that a snapshot automount returns the guid of the snapshot.
# 7. Verify that a snapshot received into the same pool returns the same
#    UUID as its source snapshot, and that the received filesystem
#    returns its own dataset guid.
# 8. Verify that a clone returns the same pool half and its own dataset
#    guid, different from the origin filesystem and snapshot guids.
#

verify_runnable "both"

typeset snapname=getfsuuid_snap
typeset fs2=$TESTPOOL/getfsuuid_fs2
typeset recvfs=$TESTPOOL/getfsuuid_recv
typeset clonefs=$TESTPOOL/getfsuuid_clone
typeset testfile=$TESTDIR/getfsuuid_file

function cleanup
{
	datasetexists $clonefs && destroy_dataset $clonefs
	datasetexists $recvfs && destroy_dataset $recvfs -r
	datasetexists $TESTPOOL/$TESTFS@$snapname && \
	    destroy_dataset $TESTPOOL/$TESTFS@$snapname
	datasetexists $fs2 && destroy_dataset $fs2
	rm -f $testfile
}

log_assert "FS_IOC_GETFSUUID returns the pool guid and the dataset guid"
log_onexit cleanup

typeset pool_guid=$(zpool get -Hp -o value guid $TESTPOOL)
typeset ds_guid=$(zfs get -Hp -o value guid $TESTPOOL/$TESTFS)

# Directory
get_uuid $TESTDIR
typeset dir_uuid=$uuid
log_must test "$pool_half" = "$pool_guid"
log_must test "$ds_half" = "$ds_guid"

# Regular file
log_must touch $testfile
get_uuid $testfile
log_must test "$uuid" = "$dir_uuid"

# Second filesystem: same pool half, its own dataset guid
log_must zfs create $fs2
typeset mnt2=$(get_prop mountpoint $fs2)
typeset ds2_guid=$(zfs get -Hp -o value guid $fs2)
get_uuid $mnt2
typeset fs2_uuid=$uuid
log_must test "$pool_half" = "$pool_guid"
log_must test "$ds_half" = "$ds2_guid"
log_mustnot test "$uuid" = "$dir_uuid"

# Remount: the UUID stays the same across an unmount and a mount
log_must zfs unmount $fs2
log_must zfs mount $fs2
get_uuid $mnt2
log_must test "$uuid" = "$fs2_uuid"

# Snapshot automount: same pool half, the guid of the snapshot
log_must zfs snapshot $TESTPOOL/$TESTFS@$snapname
typeset snap_guid=$(zfs get -Hp -o value guid $TESTPOOL/$TESTFS@$snapname)
typeset snapdir=$TESTDIR/.zfs/snapshot/$snapname
log_must ls $snapdir/
get_uuid $snapdir
typeset snap_uuid=$uuid
log_must test "$pool_half" = "$pool_guid"
log_must test "$ds_half" = "$snap_guid"

# Received snapshot in the same pool: the same guid as its source, thus
# the same UUID; the received filesystem has its own dataset guid
log_must eval "zfs send $TESTPOOL/$TESTFS@$snapname | zfs receive $recvfs"
typeset recv_guid=$(zfs get -Hp -o value guid $recvfs)
typeset recvmnt=$(get_prop mountpoint $recvfs)
get_uuid $recvmnt
log_must test "$pool_half" = "$pool_guid"
log_must test "$ds_half" = "$recv_guid"
log_mustnot test "$ds_half" = "$ds_guid"
log_must ls $recvmnt/.zfs/snapshot/$snapname/
get_uuid $recvmnt/.zfs/snapshot/$snapname
log_must test "$uuid" = "$snap_uuid"

# Clone: same pool half, its own dataset guid
log_must zfs clone $TESTPOOL/$TESTFS@$snapname $clonefs
typeset clone_guid=$(zfs get -Hp -o value guid $clonefs)
typeset clonemnt=$(get_prop mountpoint $clonefs)
get_uuid $clonemnt
log_must test "$pool_half" = "$pool_guid"
log_must test "$ds_half" = "$clone_guid"
log_mustnot test "$ds_half" = "$ds_guid"
log_mustnot test "$ds_half" = "$snap_guid"

log_pass "FS_IOC_GETFSUUID returns the pool guid and the dataset guid"
