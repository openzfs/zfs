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
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Verify that zfs mount should fail when mounting a mounted zfs filesystem or
# the mountpoint is busy
#
# STRATEGY:
# 1. Make a zfs filesystem mounted or mountpoint busy
# 2. Use zfs mount to mount the filesystem
# 3. Verify that zfs mount returns error
#

verify_runnable "both"

function cleanup
{
	if ! ismounted $fs; then
		log_must $ZFS mount $fs
	fi
}

log_assert "zfs mount fails with mounted filesystem or busy mountpoint"
log_onexit cleanup

fs=$TESTPOOL/$TESTFS
if ! ismounted $fs; then
	mntpt=$(get_prop mountpoint $fs)
	rm -Rf $mntpt

	log_must $ZFS mount $fs
fi

log_mustnot $ZFS mount $fs

mpt=$(get_prop mountpoint $fs)
log_must $ZFS umount $fs
curpath=`$DIRNAME $0`
cd $mpt
log_mustnot $ZFS mount $fs
cd $curpath

log_pass "zfs mount fails with mounted filesystem or busy moutpoint as expected."
