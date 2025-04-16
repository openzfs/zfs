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
# 'zpool wait' works when a replacing disks.
#
# STRATEGY:
# 1. Attach a disk to pool to form two-way mirror.
# 2. Start a replacement of the new disk.
# 3. Start 'zpool wait'.
# 4. Monitor the waiting process to make sure it returns neither too soon nor
#    too late.
# 5. Repeat 2-4, except using the '-w' flag with 'zpool replace' instead of
#    using 'zpool wait'.
#

function cleanup
{
	remove_io_delay
	kill_if_running $pid
	get_disklist $TESTPOOL | grep $DISK2 >/dev/null && \
	    log_must zpool detach $TESTPOOL $DISK2
	get_disklist $TESTPOOL | grep $DISK3 >/dev/null && \
	    log_must zpool detach $TESTPOOL $DISK3
}

function in_progress
{
	zpool status $TESTPOOL | grep 'replacing-' >/dev/null
}

typeset pid

log_onexit cleanup

log_must zpool attach -w $TESTPOOL $DISK1 $DISK2

add_io_delay $TESTPOOL

# Test 'zpool wait -t replace'
log_must zpool replace $TESTPOOL $DISK2 $DISK3
log_bkgrnd zpool wait -t replace $TESTPOOL
pid=$!
check_while_waiting $pid in_progress

# Test 'zpool replace -w'
log_bkgrnd zpool replace -w $TESTPOOL $DISK3 $DISK2
pid=$!
while ! is_pool_resilvering $TESTPOOL && proc_exists $pid; do
	log_must sleep .5
done
check_while_waiting $pid in_progress

log_pass "'zpool wait -t replace' and 'zpool replace -w' work."
