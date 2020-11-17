#!/bin/ksh -p
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
# 'zpool wait' works when a replacing disk is detached before the replacement
# completes.
#
# STRATEGY:
# 1. Attach a disk to pool to form two-way mirror.
# 2. Modify tunable so that resilver won't complete while test is running.
# 3. Start a replacement of the new disk.
# 4. Start a process that waits for the replace.
# 5. Wait a few seconds and then check that the wait process is actually
#    waiting.
# 6. Cancel the replacement by detaching the replacing disk.
# 7. Check that the wait process returns reasonably promptly.
#

function cleanup
{
	log_must set_tunable32 SCAN_SUSPEND_PROGRESS 0
	kill_if_running $pid
	get_disklist $TESTPOOL | grep $DISK2 >/dev/null && \
	    log_must zpool detach $TESTPOOL $DISK2
	get_disklist $TESTPOOL | grep $DISK3 >/dev/null && \
	    log_must zpool detach $TESTPOOL $DISK3
	log_must zpool sync $TESTPOOL
}

typeset pid

log_onexit cleanup

log_must zpool attach -w $TESTPOOL $DISK1 $DISK2

log_must set_tunable32 SCAN_SUSPEND_PROGRESS 1

log_must zpool replace $TESTPOOL $DISK2 $DISK3
log_bkgrnd zpool wait -t replace $TESTPOOL
pid=$!

log_must sleep 3
proc_must_exist $pid

log_must zpool detach $TESTPOOL $DISK3
bkgrnd_proc_succeeded $pid

log_pass "'zpool wait -t replace' returns when replacing disk is detached."
