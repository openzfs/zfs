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
# With the zfs_sb_uuid module parameter set to 0, a filesystem that is
# mounted afterwards has no UUID and the FS_IOC_GETFSUUID ioctl fails
# with ENOTTY; a filesystem that was mounted before keeps its UUID.
#
# STRATEGY:
# 1. Create a filesystem and get its UUID.
# 2. Set zfs_sb_uuid to 0.
# 3. Verify that the mounted filesystem keeps its UUID.
# 4. Unmount and mount the filesystem, and verify that the ioctl fails
#    with ENOTTY.
# 5. Set zfs_sb_uuid to 1, unmount and mount the filesystem, and verify
#    that the filesystem has its UUID again.
#

verify_runnable "global"

typeset fs=$TESTPOOL/getfsuuid_param

function cleanup
{
	datasetexists $fs && destroy_dataset $fs
	log_must restore_tunable SB_UUID
}

log_assert "zfs_sb_uuid=0 leaves the UUID of later mounted filesystems null"
log_onexit cleanup

log_must save_tunable SB_UUID
log_must zfs create $fs
typeset mnt=$(get_prop mountpoint $fs)

get_uuid $mnt
typeset old_uuid=$uuid

log_must set_tunable32 SB_UUID 0

# The mounted filesystem keeps its UUID
get_uuid $mnt
log_must test "$uuid" = "$old_uuid"

# A filesystem that is mounted with zfs_sb_uuid=0 has no UUID, so the
# ioctl fails with ENOTTY
log_must zfs unmount $fs
log_must zfs mount $fs
verify_no_uuid $mnt

# A filesystem that is mounted with zfs_sb_uuid=1 has its UUID again
log_must set_tunable32 SB_UUID 1
log_must zfs unmount $fs
log_must zfs mount $fs
get_uuid $mnt
log_must test "$uuid" = "$old_uuid"

log_pass "zfs_sb_uuid=0 leaves the UUID of later mounted filesystems null"
