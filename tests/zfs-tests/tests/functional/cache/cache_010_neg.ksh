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
# Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/cache/cache.cfg
. $STF_SUITE/tests/functional/cache/cache.kshlib

#
# DESCRIPTION:
#	Verify cache device must be a block device.
#
# STRATEGY:
#	1. Create a pool
#	2. Add different object as cache
#	3. Verify character devices and files fail
#

verify_runnable "global"

function cleanup_testenv
{
	cleanup
	if [[ -n $lofidev ]]; then
		if is_linux; then
			losetup -d $lofidev
		else
			lofiadm -d $lofidev
		fi
	fi
}

log_assert "Cache device can only be block devices."
log_onexit cleanup_testenv

TESTVOL=testvol1$$
dsk1=${DISKS%% *}
log_must zpool create $TESTPOOL ${DISKS#$dsk1}

# Add nomal ${DEV_RDSKDIR} device
log_must zpool add $TESTPOOL cache \
    ${DEV_RDSKDIR}/${dsk1}${SLICE_PREFIX}${SLICE0}
log_must verify_cache_device $TESTPOOL $dsk1 'ONLINE'

# Add normal file
log_mustnot zpool add $TESTPOOL cache $VDEV2

# Add /dev/rlofi device (allowed under Linux)
if is_linux; then
	lofidev=$(losetup -f)
	lofidev=${lofidev##*/}
	log_must losetup $lofidev ${VDEV2%% *}
	log_must zpool add $TESTPOOL cache $lofidev
	log_must zpool remove $TESTPOOL $lofidev
	log_must losetup -d $lofidev
	lofidev=""
else
	lofidev=${VDEV2%% *}
	log_must lofiadm -a $lofidev
	lofidev=$(lofiadm $lofidev)
	log_mustnot zpool add $TESTPOOL cache "/dev/rlofi/${lofidev#/dev/lofi/}"
	log_must lofiadm -d $lofidev
	lofidev=""
fi

# Add /dev/zvol/rdsk device (allowed under Linux)
if ! is_linux; then
	log_must zpool create $TESTPOOL2 $VDEV2
	log_must zfs create -V $SIZE $TESTPOOL2/$TESTVOL
	log_mustnot zpool add $TESTPOOL cache \
	    ${ZVOL_RDEVDIR}/$TESTPOOL2/$TESTVOL
fi

log_pass "Cache device can only be block devices."
