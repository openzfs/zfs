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
# 'zpool wait' works when waiting for resilvering to complete.
#
# STRATEGY:
# 1. Attach a device to the pool so that resilvering starts.
# 2. Start 'zpool wait'.
# 3. Monitor the waiting process to make sure it returns neither too soon nor
#    too late.
# 4. Repeat 1-3, except using the '-w' flag with 'zpool attach' instead of using
#    'zpool wait'.
#

function cleanup
{
	remove_io_delay
	kill_if_running $pid
	get_disklist $TESTPOOL | grep $DISK2 >/dev/null && \
	    log_must zpool detach $TESTPOOL $DISK2
}

typeset -r IN_PROGRESS_CHECK="is_pool_resilvering $TESTPOOL"
typeset pid

log_onexit cleanup

add_io_delay $TESTPOOL

# Test 'zpool wait -t resilver'
log_must zpool attach $TESTPOOL $DISK1 $DISK2
log_bkgrnd zpool wait -t resilver $TESTPOOL
pid=$!
check_while_waiting $pid "$IN_PROGRESS_CHECK"

log_must zpool detach $TESTPOOL $DISK2

# Test 'zpool attach -w'
log_bkgrnd zpool attach -w $TESTPOOL $DISK1 $DISK2
pid=$!
while ! is_pool_resilvering $TESTPOOL && proc_exists $pid; do
	log_must sleep .5
done
check_while_waiting $pid "$IN_PROGRESS_CHECK"

log_pass "'zpool wait -t resilver' and 'zpool attach -w' work."
