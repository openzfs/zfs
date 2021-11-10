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
# -w flag for 'zpool initialize' waits for the completion of all and only those
# initializations kicked off by that invocation.
#
# STRATEGY:
# 1. Create a pool with 3 disks.
# 2. Start initializing disks 1 and 2 with one invocation of
#    'zpool initialize -w'
# 3. Start initializing disk 3 with a second invocation of 'zpool initialize -w'
# 4. Cancel the initialization of disk 1. Check that neither waiting process
#    exits.
# 5. Cancel the initialization of disk 3. Check that only the second waiting
#    process exits.
# 6. Cancel the initialization of disk 2. Check that the first waiting process
#    exits.
#

function cleanup
{
	kill_if_running $init12_pid
	kill_if_running $init3_pid
	poolexists $TESTPOOL && destroy_pool $TESTPOOL

	[[ "$default_chunk_sz" ]] &&
	    log_must set_tunable64 INITIALIZE_CHUNK_SIZE $default_chunk_sz
}

typeset init12_pid init3_pid default_chunk_sz

log_onexit cleanup

log_must zpool create -f $TESTPOOL $DISK1 $DISK2 $DISK3

# Make sure the initialization takes a while
default_chunk_sz=$(get_tunable INITIALIZE_CHUNK_SIZE)
log_must set_tunable64 INITIALIZE_CHUNK_SIZE 512

log_bkgrnd zpool initialize -w $TESTPOOL $DISK1 $DISK2
init12_pid=$!
log_bkgrnd zpool initialize -w $TESTPOOL $DISK3
init3_pid=$!

# Make sure that we are really waiting
log_must sleep 3
proc_must_exist $init12_pid
proc_must_exist $init3_pid

#
# Cancel initialization of one of disks started by init12, make sure neither
# process exits
#
log_must zpool initialize -c $TESTPOOL $DISK1
proc_must_exist $init12_pid
proc_must_exist $init3_pid

#
# Cancel initialization started by init3, make sure that process exits, but
# init12 doesn't
#
log_must zpool initialize -c $TESTPOOL $DISK3
proc_must_exist $init12_pid
bkgrnd_proc_succeeded $init3_pid

# Cancel last initialization started by init12, make sure it returns.
log_must zpool initialize -c $TESTPOOL $DISK2
bkgrnd_proc_succeeded $init12_pid

log_pass "'zpool initialize -w' works."
