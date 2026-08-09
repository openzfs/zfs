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
