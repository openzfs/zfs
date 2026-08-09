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
else
	lofidev=${LDEV2%% *}
	log_must lofiadm -a $lofidev
	lofidev=$(lofiadm $lofidev)
fi
log_must zpool add $TESTPOOL log $lofidev
log_must verify_slog_device $TESTPOOL $lofidev 'ONLINE'

log_pass "Verify slog device can be disk, file, lofi device or any device " \
	"that presents a block interface."
