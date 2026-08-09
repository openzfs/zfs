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
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Verify that zfs mount should fail when mounting a mounted zfs filesystem or
# the mountpoint is busy.  On Linux the mount should succeed.
#
# STRATEGY:
# 1. Make a zfs filesystem mounted or mountpoint busy
# 2. Use zfs mount to mount the filesystem
# 3. Verify that zfs mount succeeds on Linux and fails for other platforms
#

verify_runnable "both"

function cleanup
{
	if ! ismounted $fs; then
		log_must zfs mount $fs
	fi
}

log_assert "zfs mount fails with mounted filesystem or busy mountpoint"
log_onexit cleanup

fs=$TESTPOOL/$TESTFS
if ! ismounted $fs; then
	log_must zfs mount $fs
fi

log_mustnot zfs mount $fs

mpt=$(get_prop mountpoint $fs)
log_must zfs umount $fs
curpath=`dirname $0`
cd $mpt
if is_linux || is_freebsd; then
    log_must zfs mount $fs
else
    log_mustnot zfs mount $fs
fi
cd $curpath

log_pass "zfs mount fails with mounted filesystem or busy mountpoint as expected."
