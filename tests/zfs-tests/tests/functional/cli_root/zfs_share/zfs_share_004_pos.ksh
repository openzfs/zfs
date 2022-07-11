#!/bin/ksh -p
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or https://opensource.org/licenses/CDDL-1.0.
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
# Copyright (c) 2012, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zfs_share/zfs_share.cfg

#
# DESCRIPTION:
# Verify that a file system and its snapshot are shared.
#
# STRATEGY:
# 1. Create a file system
# 2. Set the sharenfs property on the file system
# 3. Create a snapshot
# 4. Verify that both are shared.
#

verify_runnable "global"

function cleanup
{
	snapexists $TESTPOOL/$TESTFS@snapshot && \
		destroy_dataset $TESTPOOL/$TESTFS@snapshot

	log_must zfs set sharenfs=off $TESTPOOL/$TESTFS
	log_must unshare_fs $TESTPOOL/$TESTFS
}

#
# Main test routine.
#
# Given a mountpoint and file system this routine will attempt
# share the mountpoint and then verify a snapshot of the mounpoint
# is also shared.
#
function test_snap_share # mntp filesystem
{
        typeset mntp=$1
        typeset filesystem=$2

        not_shared $mntp || \
            log_fail "File system $filesystem is already shared."

        log_must zfs set sharenfs=on $filesystem
        is_shared $mntp || \
            log_fail "File system $filesystem is not shared (set sharenfs)."

	log_must ls -l  $mntp/$SNAPROOT/snapshot
        #
        # Verify 'zfs share' works as well.
        #
        log_must zfs unshare $filesystem
        log_must zfs share $filesystem

        is_shared $mntp || \
            log_fail "file system $filesystem is not shared (zfs share)."

	log_must ls -l  $mntp/$SNAPROOT/snapshot
}

log_assert "Verify that a file system and its snapshot are shared."
log_onexit cleanup

log_must zfs snapshot $TESTPOOL/$TESTFS@snapshot
test_snap_share $TESTDIR $TESTPOOL/$TESTFS

log_pass "A file system and its snapshot are both shared as expected."
