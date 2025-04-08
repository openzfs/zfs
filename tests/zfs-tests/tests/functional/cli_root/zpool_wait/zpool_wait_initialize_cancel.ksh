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
# 'zpool wait' works when an initialization operation is canceled.
#
# STRATEGY:
# 1. Create a pool.
# 2. Modify a tunable to make sure initializing is slow enough that it won't
#    complete before the test finishes.
# 3. Start initializing the vdev in the pool.
# 4. Start 'zpool wait'.
# 5. Wait a few seconds and then check that the wait process is actually
#    waiting.
# 6. Cancel the initialization of the device.
# 7. Check that the wait process returns reasonably promptly.
# 8. Repeat 3-7, except pause the initialization instead of canceling it.
#

function cleanup
{
	kill_if_running $pid
	poolexists $TESTPOOL && destroy_pool $TESTPOOL

	[[ "$default_chunk_sz" ]] &&
	    log_must set_tunable64 INITIALIZE_CHUNK_SIZE $default_chunk_sz
}

function do_test
{
	typeset stop_cmd=$1

	log_must zpool initialize $TESTPOOL $DISK1

	log_bkgrnd zpool wait -t initialize $TESTPOOL
	pid=$!

	# Make sure that we are really waiting
	log_must sleep 3
	proc_must_exist $pid

	# Stop initialization and make sure process returns
	log_must eval "$stop_cmd"
	bkgrnd_proc_succeeded $pid
}

typeset pid default_chunk_sz

log_onexit cleanup

# Make sure the initialization takes a while
default_chunk_sz=$(get_tunable INITIALIZE_CHUNK_SIZE)
log_must set_tunable64 INITIALIZE_CHUNK_SIZE 512

log_must zpool create $TESTPOOL $DISK1

do_test "zpool initialize -c $TESTPOOL $DISK1"
do_test "zpool initialize -s $TESTPOOL $DISK1"

log_pass "'zpool wait' works when initialization is stopped before completion."
