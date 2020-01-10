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
# 'zpool wait' works when waiting for checkpoint discard to complete.
#
# STRATEGY:
# 1. Create a pool.
# 2. Add some data to the pool.
# 3. Checkpoint the pool and delete the data so that the space is unique to the
#    checkpoint.
# 4. Discard the checkpoint using the '-w' flag.
# 5. Monitor the waiting process to make sure it returns neither too soon nor
#    too late.
# 6. Repeat 2-5, but using 'zpool wait' instead of the '-w' flag.
#

function cleanup
{
	log_must zinject -c all
	poolexists $TESTPOOL && destroy_pool $TESTPOOL
	kill_if_running $pid

	[[ $default_mem_limit ]] && log_must set_tunable64 \
	    SPA_DISCARD_MEMORY_LIMIT $default_mem_limit
}

function do_test
{
	typeset use_wait_flag=$1

	log_must dd if=/dev/urandom of="$TESTFILE" bs=128k count=1k
	log_must zpool checkpoint $TESTPOOL

	# Make sure bulk of space is unique to checkpoint
	log_must rm "$TESTFILE"

	log_must zinject -d $DISK1 -D20:1 $TESTPOOL

	if $use_wait_flag; then
		log_bkgrnd zpool checkpoint -dw $TESTPOOL
		pid=$!

		while ! is_pool_discarding $TESTPOOL && proc_exists $pid; do
			log_must sleep .5
		done
	else
		log_must zpool checkpoint -d $TESTPOOL
		log_bkgrnd zpool wait -t discard $TESTPOOL
		pid=$!
	fi

	check_while_waiting $pid "is_pool_discarding $TESTPOOL"
	log_must zinject -c all
}

typeset -r TESTFILE="/$TESTPOOL/testfile"
typeset pid default_mem_limit

log_onexit cleanup

default_mem_limit=$(get_tunable SPA_DISCARD_MEMORY_LIMIT)
log_must set_tunable64 SPA_DISCARD_MEMORY_LIMIT 32

log_must zpool create $TESTPOOL $DISK1

do_test true
do_test false

log_pass "'zpool wait -t discard' and 'zpool checkpoint -dw' work."
