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
# http://www.illumos.org/license/CDDL.
#

#
# Copyright (c) 2018 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_wait/zpool_wait.kshlib

#
# DESCRIPTION:
# 'zpool wait' works when device removal is canceled.
#
# STRATEGY:
# 1. Create a pool with two disks and some data.
# 2. Modify a tunable to make sure removal won't complete while test is running.
# 3. Start removing one of the disks.
# 4. Start 'zpool wait'.
# 5. Sleep for a few seconds and check that the process is actually waiting.
# 6. Cancel the removal of the device.
# 7. Check that the wait process returns reasonably promptly.
#

function cleanup
{
	kill_if_running $pid
	log_must set_tunable32 REMOVAL_SUSPEND_PROGRESS 0
	poolexists $TESTPOOL && destroy_pool $TESTPOOL
}

log_onexit cleanup

typeset pid

log_must zpool create -f $TESTPOOL $DISK1 $DISK2

log_must dd if=/dev/urandom of="/$TESTPOOL/testfile" bs=1k count=16k

# Start removal, but don't allow it to make any progress
log_must set_tunable32 REMOVAL_SUSPEND_PROGRESS 1
log_must zpool remove $TESTPOOL $DISK1

log_bkgrnd zpool wait -t remove $TESTPOOL
pid=$!

log_must sleep 3
proc_must_exist $pid

log_must zpool remove -s $TESTPOOL
bkgrnd_proc_succeeded $pid

log_pass "'zpool wait -t remove' works when removal is canceled."
