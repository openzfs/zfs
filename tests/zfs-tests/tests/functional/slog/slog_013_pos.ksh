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

log_assert "Verify slog device can be disk, file, lofi device or any device " \
	"that presents a block interface."
verify_disk_count "$DISKS" 2
log_onexit cleanup_testenv
log_must setup

dsk1=${DISKS%% *}
log_must zpool create $TESTPOOL ${DISKS#$dsk1}

# Add provided disk
log_must zpool add $TESTPOOL log $dsk1
log_must verify_slog_device $TESTPOOL $dsk1 'ONLINE'
# Add normal file
log_must zpool add $TESTPOOL log $LDEV
ldev=$(random_get $LDEV)
log_must verify_slog_device $TESTPOOL $ldev 'ONLINE'

# Add loop back device
if is_linux; then
	lofidev=$(losetup -f)
	log_must losetup $lofidev ${LDEV2%% *}
	lofidev=${lofidev##*/}
elif is_freebsd; then
	lofidev=$(mdconfig -a ${LDEV2%% *})
elif is_netbsd; then
	# vnd(4) is NetBSD's vnode disk -- same role as mdconfig/losetup.
	# vnd0 often backs /mnt/zts; prefer a free unit.
	lofidev=
	typeset i
	for i in 1 2 3 0 7; do
		[ -b /dev/vnd${i}d ] || (cd /dev && sh MAKEDEV vnd$i)
		if vnconfig -l vnd$i 2>/dev/null | grep -q "not in use"; then
			log_must vnconfig -c vnd$i ${LDEV2%% *}
			lofidev=/dev/vnd${i}d
			break
		fi
	done
	[ -n "$lofidev" ] || log_fail "no free vnd for slog lofi"
else
	lofidev=${LDEV2%% *}
	log_must lofiadm -a $lofidev
	lofidev=$(lofiadm $lofidev)
fi
log_must zpool add $TESTPOOL log $lofidev
# Disk vdevs are printed as the /dev leaf.  NetBSD's vnd path is
# /dev/vndNd; status shows vndNd.
if is_netbsd; then
	log_must verify_slog_device $TESTPOOL ${lofidev##*/} 'ONLINE'
else
	log_must verify_slog_device $TESTPOOL $lofidev 'ONLINE'
fi

log_pass "Verify slog device can be disk, file, lofi device or any device " \
	"that presents a block interface."
