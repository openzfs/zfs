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
#	Re-creating zfs files, 'zfs unmount' still succeed.
#
# STRATEGY:
#	1. Create pool and filesystem.
#	2. Recreating the same file in this fs for a while, then breaking out.
#	3. Verify the filesystem can be unmount successfully.
#

verify_runnable "both"

function cleanup
{
	if ! ismounted $TESTPOOL/$TESTFS ; then
		log_must zfs mount $TESTPOOL/$TESTFS
	fi
}

log_assert "Re-creating zfs files, 'zfs unmount' still succeed."
log_onexit cleanup

# Call cleanup to make sure the file system are mounted.
cleanup
mntpnt=$(get_prop mountpoint $TESTPOOL/$TESTFS)

typeset -i i=0
while (( i < 10000 )); do
	cp $STF_SUITE/include/libtest.shlib $mntpnt

	(( i += 1 ))
done
log_note "Recreating zfs files for 10000 times."

log_must zfs unmount $TESTPOOL/$TESTFS

log_pass "Re-creating zfs files, 'zfs unmount' passed."
