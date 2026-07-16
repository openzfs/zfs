#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
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
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_export/zpool_export.cfg

#
# DESCRIPTION:
# Exported pools should no longer be visible from 'zpool list'.
# Therefore, we export an existing pool and verify it cannot
# be accessed.
#
# STRATEGY:
# 1. Create a pool and filesystem for testing.
# 2. Unmount the test directory.
# 3. Export the pool.
# 4. Verify the pool is no longer present in the list output.
#

verify_runnable "global"

function cleanup
{
	if poolexists $TESTPOOL; then
		destroy_pool $TESTPOOL
	fi
	[[ -d /$TESTPOOL ]] && rm -rf /$TESTPOOL
	[[ -d $TESTDIR ]] && rm -rf $TESTDIR
}

log_onexit cleanup

log_assert "Verify a pool can be exported."

# Set up the pool and filesystem manually
DISK=${DISKS%% *}

# Clean up any existing pool
if poolexists $TESTPOOL; then
	destroy_pool $TESTPOOL
fi
[[ -d /$TESTPOOL ]] && rm -rf /$TESTPOOL

# Create the pool
log_must zpool create -f $TESTPOOL $DISK

# Create test directory
rm -rf $TESTDIR || log_unresolved "Could not remove $TESTDIR"
mkdir -p $TESTDIR || log_unresolved "Could not create $TESTDIR"

# Create filesystem with mountpoint
log_must zfs create $TESTPOOL/$TESTFS
log_must zfs set mountpoint=$TESTDIR $TESTPOOL/$TESTFS

log_must zfs umount $TESTDIR
log_must zpool export $TESTPOOL

poolexists $TESTPOOL && \
        log_fail "$TESTPOOL unexpectedly found in 'zpool list' output."

log_pass "Successfully exported a ZPOOL."
