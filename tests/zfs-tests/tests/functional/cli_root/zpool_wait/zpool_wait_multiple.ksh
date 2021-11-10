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
# 'zpool wait' works when waiting for multiple activities.
#
# STRATEGY:
# 1. Create a pool with some data.
# 2. Alterate running two different activities (scrub and initialize),
#    making sure that they overlap such that one of the two is always
#    running.
# 3. Wait for both activities with a single invocation of zpool wait.
# 4. Check that zpool wait doesn't return until both activities have
#    stopped.
#

function cleanup
{
	kill_if_running $pid
	poolexists $TESTPOOL && destroy_pool $TESTPOOL

	[[ "$default_chunk_sz" ]] && log_must set_tunable64 \
	    INITIALIZE_CHUNK_SIZE $default_chunk_sz
	log_must set_tunable32 SCAN_SUSPEND_PROGRESS 0
}

typeset pid default_chunk_sz

log_onexit cleanup

log_must zpool create -f $TESTPOOL $DISK1
log_must dd if=/dev/urandom of="/$TESTPOOL/testfile" bs=64k count=1k

default_chunk_sz=$(get_tunable INITIALIZE_CHUNK_SIZE)
log_must set_tunable64 INITIALIZE_CHUNK_SIZE 512
log_must set_tunable32 SCAN_SUSPEND_PROGRESS 1

log_must zpool scrub $TESTPOOL

log_bkgrnd zpool wait -t scrub,initialize $TESTPOOL
pid=$!

log_must sleep 2

log_must zpool initialize $TESTPOOL $DISK1
log_must zpool scrub -s $TESTPOOL

log_must sleep 2

log_must zpool scrub $TESTPOOL
log_must zpool initialize -s $TESTPOOL $DISK1

log_must sleep 2

log_must zpool initialize $TESTPOOL $DISK1
log_must zpool scrub -s $TESTPOOL

log_must sleep 2

proc_must_exist $pid

# Cancel last activity, zpool wait should return
log_must zpool initialize -s $TESTPOOL $DISK1
bkgrnd_proc_succeeded $pid

log_pass "'zpool wait' works when waiting for multiple activities."
