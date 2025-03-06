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
# Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/migration/migration.cfg

verify_runnable "global"

ismounted $NONZFS_TESTDIR $NEWFS_DEFAULT_FS && log_must umount -f $NONZFS_TESTDIR

ismounted $TESTPOOL/$TESTFS && log_must zfs umount -f $TESTDIR
destroy_pool $TESTPOOL

DISK=${DISKS%% *}
if is_mpath_device $DISK; then
        delete_partitions
fi

# recreate and destroy a zpool over the disks to restore the partitions to
# normal
case $DISK_COUNT in
0)
	log_note "No disk devices to restore"
	;;
1)
	log_must cleanup_devices $ZFS_DISK
	;;
*)
	log_must cleanup_devices $ZFS_DISK $NONZFS_DISK
	;;
esac

log_pass
