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
# 'zpool wait' works when waiting for a device to be removed.
#
# STRATEGY:
# 1. Create a pool with two disks and some data.
# 2. Modify a tunable to make sure removal doesn't make any progress.
# 3. Start removing one of the disks.
# 4. Start 'zpool wait'.
# 5. Sleep for a few seconds and check that the process is actually waiting.
# 6. Modify tunable to allow removal to complete.
# 7. Monitor the waiting process to make sure it returns neither too soon nor
#    too late.
# 8. Repeat 1-7, except using the '-w' flag for 'zpool remove' instead of using
#    'zpool wait'.
#

function cleanup
{
	kill_if_running $pid
	log_must set_tunable32 REMOVAL_SUSPEND_PROGRESS 0
	poolexists $TESTPOOL && destroy_pool $TESTPOOL
}

function do_test
{
	typeset use_flag=$1

	log_must zpool create -f $TESTPOOL $DISK1 $DISK2
	log_must dd if=/dev/urandom of="/$TESTPOOL/testfile" bs=1k count=16k

	# Start removal, but don't allow it to make any progress at first
	log_must set_tunable32 REMOVAL_SUSPEND_PROGRESS 1

	if $use_flag; then
		log_bkgrnd zpool remove -w $TESTPOOL $DISK1
		pid=$!

		while ! is_pool_removing $TESTPOOL && proc_exists $pid; do
			log_must sleep .5
		done
	else
		log_must zpool remove $TESTPOOL $DISK1
		log_bkgrnd zpool wait -t remove $TESTPOOL
		pid=$!
	fi

	# Make sure the 'zpool wait' is actually waiting
	log_must sleep 3
	proc_must_exist $pid

	# Unpause removal, and wait for it to finish
	log_must set_tunable32 REMOVAL_SUSPEND_PROGRESS 0
	check_while_waiting $pid "is_pool_removing $TESTPOOL"

	log_must zpool destroy $TESTPOOL
}

log_onexit cleanup

typeset pid

do_test true
do_test false

log_pass "'zpool wait -t remove' and 'zpool remove -w' work."
