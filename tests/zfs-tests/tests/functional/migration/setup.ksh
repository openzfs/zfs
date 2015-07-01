#!/bin/ksh -p
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

set_partition ${ZFSSIDE_DISK##*[sp]} "" $FS_SIZE $ZFS_DISK
set_partition ${NONZFSSIDE_DISK##*[sp]} "" $FS_SIZE $NONZFS_DISK

if [[ -n "$LINUX" ]]; then
	typeset -i i=0
	for dsk in $ZFS_DISK  $NONZFS_DISK; do
		# Setup partition mappings with kpartx
		set -- $($KPARTX -asfv $dsk | head -n1)
		eval typeset loop$i="${8##*/}"
		((i += 1))
	done

	# Override variables
	ZFSSIDE_DISK="/dev/mapper/$loop0"p1
	NONZFSSIDE_DISK="/dev/mapper/$loop1"p1
fi

create_pool $TESTPOOL "$ZFSSIDE_DISK"

$RM -rf $TESTDIR  || log_unresolved Could not remove $TESTDIR
$MKDIR -p $TESTDIR || log_unresolved Could not create $TESTDIR

log_must $ZFS create $TESTPOOL/$TESTFS
log_must $ZFS set mountpoint=$TESTDIR $TESTPOOL/$TESTFS

$RM -rf $NONZFS_TESTDIR  || log_unresolved Could not remove $NONZFS_TESTDIR
$MKDIR -p $NONZFS_TESTDIR || log_unresolved Could not create $NONZFS_TESTDIR

if [[ -n "$LINUX" ]]; then
	$NEWFS $NONZFSSIDE_DISK >/dev/null 2>&1
else
	$ECHO "y" | $NEWFS -v $DEV_RDSKDIR/$NONZFSSIDE_DISK
fi
(( $? != 0 )) &&
	log_untested "Unable to setup a $NEWFS_DEFAULT_FS file system"

if [[ -n "$LINUX" ]]; then
	log_must $MOUNT $NONZFSSIDE_DISK $NONZFS_TESTDIR
else
	log_must $MOUNT $DEV_DSKDIR/$NONZFSSIDE_DISK $NONZFS_TESTDIR
fi

log_pass
