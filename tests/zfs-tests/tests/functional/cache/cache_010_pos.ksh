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
		elif is_netbsd; then
			typeset v=${lofidev#/dev/}
			v=${v#r}
			v=${v%%[a-p]}
			vnconfig -u $v
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

# Add the first disk/file as cache. Never prefix /dev/ onto an
# already-absolute file vdev (that produced /dev//build/...).
log_must zpool add $TESTPOOL cache $(to_usable_path $dsk1)
log_must zpool remove $TESTPOOL $(to_usable_path $dsk1)


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
elif is_netbsd; then
	# vnd(4) is NetBSD's vnode disk -- same role as mdconfig/losetup.
	# vnd0 backs /mnt/zts; use vnd1.
	lofidev=vnd1
	[ -b /dev/${lofidev}d ] || (cd /dev && sh MAKEDEV $lofidev)
	log_must vnconfig -c $lofidev ${VDEV2%% *}
	lofidev=/dev/${lofidev}d
else
	lofidev=${VDEV2%% *}
	log_must lofiadm -a $lofidev
	lofidev=$(lofiadm $lofidev)
fi

log_must zpool add $TESTPOOL cache $lofidev
log_must verify_cache_device $TESTPOOL $lofidev 'ONLINE'

log_pass "Verify cache devices can be disk, file, lofi device or any " \
  "device that presents a block interface"
