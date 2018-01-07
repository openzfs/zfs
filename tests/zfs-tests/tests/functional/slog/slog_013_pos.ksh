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
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/slog/slog.kshlib

#
# DESCRIPTION:
#	Verify slog device can be disk, file, lofi device or any device that
#	presents a block interface.
#
# STRATEGY:
#	1. Create a pool
#	2. Loop to add different object as slog
#	3. Verify it passes
#

verify_runnable "global"

if ! $(is_physical_device $DISKS) ; then
	log_unsupported "This directory cannot be run on raw files."
fi

function cleanup_testenv
{
	cleanup
	if datasetexists $TESTPOOL2 ; then
		log_must zpool destroy -f $TESTPOOL2
	fi
	if [[ -n $lofidev ]]; then
		if is_linux; then
			losetup -d $lofidev
		else
			lofiadm -d $lofidev
		fi
	fi
}

log_assert "Verify slog device can be disk, file, lofi device or any device " \
	"that presents a block interface."
verify_disk_count "$DISKS" 2
log_onexit cleanup_testenv

dsk1=${DISKS%% *}
log_must zpool create $TESTPOOL ${DISKS#$dsk1}

# Add nomal disk
log_must zpool add $TESTPOOL log $dsk1
log_must verify_slog_device $TESTPOOL $dsk1 'ONLINE'
# Add nomal file
log_must zpool add $TESTPOOL log $LDEV
ldev=$(random_get $LDEV)
log_must verify_slog_device $TESTPOOL $ldev 'ONLINE'

# Add lofi device
if is_linux; then
	lofidev=$(losetup -f)
	lofidev=${lofidev##*/}
	log_must losetup $lofidev ${LDEV2%% *}
else
	lofidev=${LDEV2%% *}
	log_must lofiadm -a $lofidev
	lofidev=$(lofiadm $lofidev)
fi
log_must zpool add $TESTPOOL log $lofidev
log_must verify_slog_device $TESTPOOL $lofidev 'ONLINE'

log_pass "Verify slog device can be disk, file, lofi device or any device " \
	"that presents a block interface."

# Add file which reside in the itself
mntpnt=$(get_prop mountpoint $TESTPOOL)
log_must mkfile $MINVDEVSIZE $mntpnt/vdev
log_must zpool add $TESTPOOL $mntpnt/vdev

# Add ZFS volume
vol=$TESTPOOL/vol
log_must zpool create -V $MINVDEVSIZE $vol
log_must zpool add $TESTPOOL ${ZVOL_DEVDIR}/$vol
