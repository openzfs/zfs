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
# Copyright (c) 2025 by Heonje LEE. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_export/zpool_export.kshlib

#
# DESCRIPTION:
#	Exporting a pool with a mounted zvol block device must fail with
#	EBUSY rather than hanging the system in an uninterruptible sleep.
#	This mirrors the behavior already provided for mounted ZFS
#	filesystems (see zpool_export_002_pos.ksh).
#
# STRATEGY:
#	1. Create a pool with a zvol.
#	2. Format and mount the zvol block device.
#	3. Attempt to export the pool -- must fail (EBUSY).
#	4. Unmount the zvol.
#	5. Export should now succeed.
#

verify_runnable "global"

function cleanup
{
	ismounted $mntpnt $NEWFS_DEFAULT_FS && log_must umount $mntpnt
	[[ -d $mntpnt ]] && log_must rmdir $mntpnt
	datasetexists $TESTPOOL1/vol && log_must zfs destroy $TESTPOOL1/vol
	poolexists $TESTPOOL1 && destroy_pool $TESTPOOL1
	[[ -f $vdev ]] && log_must rm -f $vdev
	zpool_export_cleanup
}

mntpnt=$TESTDIR0/zvol_export_mnt
vdev=$TESTDIR0/zvol_export_vdev

log_onexit cleanup

log_assert "Exporting a pool with a mounted zvol returns EBUSY"

log_must mkdir -p $TESTDIR0
log_must truncate -s $MINVDEVSIZE $vdev

# 1. Create pool + zvol
log_must zpool create $TESTPOOL1 $vdev
log_must zfs create -V 100M $TESTPOOL1/vol
block_device_wait ${ZVOL_DEVDIR}/$TESTPOOL1/vol

# 2. Format and mount the zvol block device
log_must eval "new_fs ${ZVOL_RDEVDIR}/$TESTPOOL1/vol >/dev/null 2>&1"
log_must mkdir -p $mntpnt
log_must mount ${ZVOL_DEVDIR}/$TESTPOOL1/vol $mntpnt

# 3. Export must fail with EBUSY (not hang)
log_mustnot zpool export $TESTPOOL1
log_must poolexists $TESTPOOL1

# 4. Unmount the zvol
log_must umount $mntpnt

# 5. Export should now succeed
log_must zpool export $TESTPOOL1
log_mustnot poolexists $TESTPOOL1

log_pass "Export with mounted zvol correctly returned EBUSY"
