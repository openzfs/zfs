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

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# The FS_IOC_GETFSUUID ioctl returns a UUID that contains the pool guid
# and the dataset guid, on regular files and on directories, for
# filesystems, for snapshot automounts, and for clones.
#
# STRATEGY:
# 1. Get the UUID of the mountpoint directory of a filesystem.
# 2. Verify that the two UUID halves equal the pool and dataset guids.
# 3. Verify that a regular file returns the same UUID.
# 4. Verify that a second filesystem returns the same pool half and its
#    own dataset guid.
# 5. Verify that the UUID stays the same after an unmount and a mount.
# 6. Verify that a snapshot automount returns the guid of the snapshot.
# 7. Verify that a clone returns the same pool half and its own dataset
#    guid, different from the origin filesystem and snapshot guids.
#

verify_runnable "both"

typeset snapname=getfsuuid_snap
typeset fs2=$TESTPOOL/getfsuuid_fs2
typeset clonefs=$TESTPOOL/getfsuuid_clone
typeset testfile=$TESTDIR/getfsuuid_file

function cleanup
{
	datasetexists $clonefs && destroy_dataset $clonefs
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
typeset out
out=$(getfsuuid $TESTDIR) || log_fail "getfsuuid failed on $TESTDIR"
set -A dir_res -- $out
log_note "uuid of $TESTDIR: ${dir_res[0]}"
log_must test "${dir_res[1]}" = "$pool_guid"
log_must test "${dir_res[2]}" = "$ds_guid"

# Regular file
log_must touch $testfile
out=$(getfsuuid $testfile) || log_fail "getfsuuid failed on $testfile"
set -A file_res -- $out
log_must test "${file_res[0]}" = "${dir_res[0]}"

# Second filesystem: same pool half, its own dataset guid
log_must zfs create $fs2
typeset mnt2=$(get_prop mountpoint $fs2)
typeset ds2_guid=$(zfs get -Hp -o value guid $fs2)
out=$(getfsuuid $mnt2) || log_fail "getfsuuid failed on $mnt2"
set -A fs2_res -- $out
log_must test "${fs2_res[1]}" = "$pool_guid"
log_must test "${fs2_res[2]}" = "$ds2_guid"
log_mustnot test "${fs2_res[0]}" = "${dir_res[0]}"

# Remount: the UUID stays the same across an unmount and a mount
log_must zfs unmount $fs2
log_must zfs mount $fs2
out=$(getfsuuid $mnt2) || log_fail "getfsuuid failed on $mnt2"
set -A remount_res -- $out
log_must test "${remount_res[0]}" = "${fs2_res[0]}"

# Snapshot automount: same pool half, the guid of the snapshot
log_must zfs snapshot $TESTPOOL/$TESTFS@$snapname
typeset snap_guid=$(zfs get -Hp -o value guid $TESTPOOL/$TESTFS@$snapname)
typeset snapdir=$TESTDIR/.zfs/snapshot/$snapname
log_must ls $snapdir/
out=$(getfsuuid $snapdir) || log_fail "getfsuuid failed on $snapdir"
set -A snap_res -- $out
log_must test "${snap_res[1]}" = "$pool_guid"
log_must test "${snap_res[2]}" = "$snap_guid"

# Clone: same pool half, its own dataset guid
log_must zfs clone $TESTPOOL/$TESTFS@$snapname $clonefs
typeset clone_guid=$(zfs get -Hp -o value guid $clonefs)
typeset clonemnt=$(get_prop mountpoint $clonefs)
out=$(getfsuuid $clonemnt) || log_fail "getfsuuid failed on $clonemnt"
set -A clone_res -- $out
log_must test "${clone_res[1]}" = "$pool_guid"
log_must test "${clone_res[2]}" = "$clone_guid"
log_mustnot test "${clone_res[2]}" = "$ds_guid"
log_mustnot test "${clone_res[2]}" = "$snap_guid"

log_pass "FS_IOC_GETFSUUID returns the pool guid and the dataset guid"
