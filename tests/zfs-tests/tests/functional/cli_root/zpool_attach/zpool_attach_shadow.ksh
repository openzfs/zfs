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
# Copyright (c) 2026 Klara, Inc.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Attaching a shadow mirror works correctly
#
# STRATEGY:
# 1. Create a single-disk pool.
# 2. Attach a second disk as a shadow mirror.
# 3. Verify basic pool functionality.
#

verify_runnable "global"

function cleanup
{
	zpool destroy $TESTPOOL
	default_mirror_setup $DISKS
}

log_onexit cleanup
log_assert "Attaching a shadow mirror works correctly"

echo $DISKS | read DISK0 DISK1 DISK2

log_must zpool destroy $TESTPOOL
log_must create_pool $TESTPOOL $DISK0
log_must zpool attach -S $TESTPOOL $DISK0 $DISK1
log_must zfs create $TESTPOOL/testfs
typeset mtpt=$(get_prop mountpoint $TESTPOOL/testfs)
log_must file_write -o create -f "$mtpt/f1" -b 65536 -c 64 -d R

log_must zinject -a
log_must zpool scrub $TESTPOOL
log_must wait_scrubbed $TESTPOOL
log_must check_pool_status $TESTPOOL "scan" "with 0 errors"
log_must check_pool_status $TESTPOOL "errors" "No known data errors"

log_must zpool export $TESTPOOL
log_must zpool import $TESTPOOL

log_pass "Shadow mirrors behave correctly"
