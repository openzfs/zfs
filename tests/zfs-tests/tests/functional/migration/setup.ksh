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
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/migration/migration.cfg

verify_runnable "global"

case $DISK_COUNT in
0)
	log_untested "Need at least 1 disk device for test"
	;;
1)
	log_note "Partitioning a single disk ($SINGLE_DISK)"
	;;
*)
	log_note "Partitioning disks ($ZFS_DISK $NONZFS_DISK)"
	;;
esac

create_pool $TESTPOOL "$ZFS_DISK"

rm -rf $TESTDIR  || log_unresolved Could not remove $TESTDIR
mkdir -p $TESTDIR || log_unresolved Could not create $TESTDIR

log_must zfs create $TESTPOOL/$TESTFS
log_must zfs set mountpoint=$TESTDIR $TESTPOOL/$TESTFS

rm -rf $NONZFS_TESTDIR  || log_unresolved Could not remove $NONZFS_TESTDIR
mkdir -p $NONZFS_TESTDIR || log_unresolved Could not create $NONZFS_TESTDIR

#
# $NONZFS_DISK may still carry a zfs_member label from a pool used by an
# earlier test.  'zpool destroy' leaves the vdev labels in place and new_fs
# only overwrites the front of the device, so the trailing labels can
# survive.  libblkid then probes the device as ambiguous (both the new
# filesystem and zfs_member) and the auto-detecting mount below fails
# intermittently.  Wipe any residual signatures first so the new filesystem
# is unambiguous.  Skip the single-disk case, where $NONZFS_DISK is the same
# device the test pool was just created on.
#
if is_linux && [[ "$NONZFS_DISK" != "$ZFS_DISK" ]]; then
	log_must wipefs -a ${DEV_DSKDIR}/$NONZFS_DISK
fi

new_fs ${DEV_DSKDIR}/$NONZFS_DISK ||
	log_untested "Unable to setup a $NEWFS_DEFAULT_FS file system"

#
# Let udev settle so the device node reflects the new filesystem before the
# auto-detecting mount consults it.
#
block_device_wait ${DEV_DSKDIR}/$NONZFS_DISK

log_must mount ${DEV_DSKDIR}/$NONZFS_DISK $NONZFS_TESTDIR

log_pass
