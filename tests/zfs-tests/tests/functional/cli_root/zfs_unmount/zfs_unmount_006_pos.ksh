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
(($? != 0)) && log_fail "get_prop mountpoint $TESTPOOL/$TESTFS"

typeset -i i=0
while (( i < 10000 )); do
	cp $STF_SUITE/include/libtest.shlib $mntpnt

	(( i += 1 ))
done
log_note "Recreating zfs files for 10000 times."

log_must zfs unmount $TESTPOOL/$TESTFS

log_pass "Re-creating zfs files, 'zfs unmount' passed."
