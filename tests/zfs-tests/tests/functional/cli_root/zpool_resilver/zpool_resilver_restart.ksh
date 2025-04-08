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
# Copyright (c) 2018 Datto Inc.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_reopen/zpool_reopen.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_resilver/zpool_resilver.cfg

#
# DESCRIPTION:
#	"Verify 'zpool resilver' restarts in-progress resilvers"
#
# STRATEGY:
#	1. Write some data and detach the first drive so it has resilver
#	   work to do
#	2. Repeat the process with a second disk
#	3. Reattach the drives, causing the second drive's resilver to be
#	   deferred
#	4. Manually restart the resilver with all drives
#

verify_runnable "global"

function cleanup
{
	log_must set_tunable32 SCAN_SUSPEND_PROGRESS 0
	log_must rm -f $mntpnt/biggerfile1
	log_must rm -f $mntpnt/biggerfile2
}

log_onexit cleanup

log_assert "Verify 'zpool resilver' restarts in-progress resilvers"

mntpnt=$(get_prop mountpoint $TESTPOOL/$TESTFS)

# 1. Write some data and detach the first drive so it has resilver work to do
log_must file_write -b 524288 -c 1024 -o create -d 0 -f $mntpnt/biggerfile1
sync_all_pools
log_must zpool detach $TESTPOOL $DISK2

# 2. Repeat the process with a second disk
log_must file_write -b 524288 -c 1024 -o create -d 0 -f $mntpnt/biggerfile2
sync_all_pools
log_must zpool detach $TESTPOOL $DISK3

# 3. Reattach the drives, causing the second drive's resilver to be deferred
log_must set_tunable32 SCAN_SUSPEND_PROGRESS 1

log_must zpool attach $TESTPOOL $DISK1 $DISK2
log_must is_pool_resilvering $TESTPOOL true

log_must zpool attach $TESTPOOL $DISK1 $DISK3
log_must is_pool_resilvering $TESTPOOL true

# 4. Manually restart the resilver with all drives
log_must zpool resilver $TESTPOOL
log_must is_deferred_scan_started $TESTPOOL
log_must set_tunable32 SCAN_SUSPEND_PROGRESS 0
log_must wait_for_resilver_end $TESTPOOL $MAXTIMEOUT
log_must check_state $TESTPOOL "$DISK2" "online"
log_must check_state $TESTPOOL "$DISK3" "online"

log_pass "Verified 'zpool resilver' restarts in-progress resilvers"
