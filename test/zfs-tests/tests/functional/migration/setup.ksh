#!/usr/bin/ksh -p
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
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
# Copyright (c) 2013 by Delphix. All rights reserved.
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

set_partition ${ZFSSIDE_DISK##*s} "" $FS_SIZE $ZFS_DISK
set_partition ${NONZFSSIDE_DISK##*s} "" $FS_SIZE $NONZFS_DISK

create_pool $TESTPOOL "$ZFSSIDE_DISK"

$RM -rf $TESTDIR  || log_unresolved Could not remove $TESTDIR
$MKDIR -p $TESTDIR || log_unresolved Could not create $TESTDIR

log_must $ZFS create $TESTPOOL/$TESTFS
log_must $ZFS set mountpoint=$TESTDIR $TESTPOOL/$TESTFS

$RM -rf $NONZFS_TESTDIR  || log_unresolved Could not remove $NONZFS_TESTDIR
$MKDIR -p $NONZFS_TESTDIR || log_unresolved Could not create $NONZFS_TESTDIR

$ECHO "y" | $NEWFS -v /dev/rdsk/$NONZFSSIDE_DISK
(( $? != 0 )) &&
	log_untested "Unable to setup a UFS file system"

log_must $MOUNT /dev/dsk/$NONZFSSIDE_DISK $NONZFS_TESTDIR

log_pass
