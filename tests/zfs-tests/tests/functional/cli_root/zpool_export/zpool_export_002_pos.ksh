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

. $STF_SUITE/tests/functional/cli_root/zpool_export/zpool_export.kshlib

#
# DESCRIPTION:
# The 'zpool export' command must fail when a pool is
# busy i.e. mounted.
#
# STRATEGY:
# 1. Create a pool and filesystem for testing.
# 2. Try and export the pool when mounted and busy.
# 3. Verify an error is returned.
#

verify_runnable "global"

function cleanup
{
	log_must cd $olddir
	zpool_export_cleanup
}

olddir=$PWD

log_onexit cleanup

log_assert "Verify a busy ZPOOL cannot be exported."

# Set up the pool and filesystem manually
DISK=${DISKS%% *}

# Clean up any existing pool
if poolexists $TESTPOOL ; then
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

log_must ismounted "$TESTPOOL/$TESTFS"
log_must cd $TESTDIR
log_mustnot zpool export $TESTPOOL
log_must poolexists $TESTPOOL

log_pass "Unable to export a busy ZPOOL as expected."
