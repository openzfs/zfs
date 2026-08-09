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
