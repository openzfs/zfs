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
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/grow/grow.cfg

#
# DESCRIPTION:
# A ZFS file system is limited by the amount of disk space
# available to the pool. Growing the pool by adding a disk
# increases the amount of space.
#
# STRATEGY:
# 1) Fill a ZFS filesystem until ENOSPC by creating a large file
# 2) Grow the pool by adding a disk
# 3) Verify that more data can now be written to the file system
#

verify_runnable "global"

function cleanup
{
	destroy_pool $TESTPOOL
	rm -f $DEVICE1 $DEVICE2 $DEVICE3
}

log_assert "A zpool may be increased in capacity by adding a disk"

log_onexit cleanup

truncate -s $SPA_MINDEVSIZE $DEVICE1 $DEVICE2
create_pool $TESTPOOL $pooltype $DEVICE1 $DEVICE2

log_must zfs create $TESTPOOL/$TESTFS
log_must zfs set mountpoint=$TESTDIR $TESTPOOL/$TESTFS
log_must zfs set compression=off $TESTPOOL/$TESTFS

file_write -o create -f $TESTDIR/$TESTFILE1 \
	-b $BLOCK_SIZE -c $WRITE_COUNT -d 0

typeset -i zret=$?
readonly ENOSPC=28

if [[ $zret -ne $ENOSPC ]]; then
	log_fail "file_write completed w/o ENOSPC, aborting!!!"
fi

if [[ ! -s $TESTDIR/$TESTFILE1 ]]; then
	log_fail "$TESTDIR/$TESTFILE1 was not created"
fi

truncate -s $SPA_MINDEVSIZE $DEVICE3
log_must zpool add $TESTPOOL $DEVICE3

log_must file_write -o append -f $TESTDIR/$TESTFILE1 \
	-b $BLOCK_SIZE -c $SMALL_WRITE_COUNT -d 0

log_must zfs inherit compression $TESTPOOL/$TESTFS

log_must destroy_pool $TESTPOOL
rm -f $DEVICE1 $DEVICE2 $DEVICE3

log_pass "TESTPOOL successfully grown"
