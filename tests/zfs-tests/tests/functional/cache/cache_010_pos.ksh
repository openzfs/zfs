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
#	Verify that cache devices can be block devices, files or character devices
#
# STRATEGY:
#	1. Create a pool
#	2. Add different object as cache
#	3. Verify character devices and files pass
#

verify_runnable "global"

function cleanup_testenv
{
	cleanup
	if [[ -n $lofidev ]]; then
		if is_linux; then
			losetup -d $lofidev
		elif is_freebsd; then
			mdconfig -du ${lofidev#md}
		else
			lofiadm -d $lofidev
		fi
	fi
}

log_assert "Verify cache devices can be disk, file, lofi device or any " \
  "device that presents a block interface"

verify_disk_count "$DISKS" 2
log_onexit cleanup_testenv

TESTVOL=testvol1$$
dsk1=${DISKS%% *}
log_must zpool create $TESTPOOL ${DISKS#$dsk1}

# Add normal ${DEV_RDSKDIR} device
log_must zpool add $TESTPOOL cache \
    ${DEV_RDSKDIR}/${dsk1}
log_must zpool remove $TESTPOOL ${DEV_RDSKDIR}/${dsk1}


# Add provided disk
log_must zpool add $TESTPOOL cache $dsk1
log_must verify_cache_device $TESTPOOL $dsk1 'ONLINE'
log_must zpool remove $TESTPOOL $dsk1

# Add normal file
log_must zpool add $TESTPOOL cache $VDEV
ldev=$(random_get $VDEV)
log_must verify_cache_device $TESTPOOL $ldev 'ONLINE'

# Add loop back device
if is_linux; then
	lofidev=$(losetup -f)
	log_must losetup $lofidev ${VDEV2%% *}
	lofidev=${lofidev##*/}
elif is_freebsd; then
	lofidev=$(mdconfig -a ${VDEV2%% *})
else
	lofidev=${VDEV2%% *}
	log_must lofiadm -a $lofidev
	lofidev=$(lofiadm $lofidev)
fi

log_must zpool add $TESTPOOL cache $lofidev
log_must verify_cache_device $TESTPOOL $lofidev 'ONLINE'

log_pass "Verify cache devices can be disk, file, lofi device or any " \
  "device that presents a block interface"
