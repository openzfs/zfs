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
# 'zpool wait' works when a scrub is paused or canceled.
#
# STRATEGY:
# 1. Modify tunable so that scrubs won't complete while test is running.
# 2. Start a scrub.
# 3. Start a process that waits for the scrub.
# 4. Wait a few seconds and then check that the wait process is actually
#    waiting.
# 5. Pause the scrub.
# 6. Check that the wait process returns reasonably promptly.
# 7. Repeat 2-6, except stop the scrub instead of pausing it.
#

function cleanup
{
	log_must set_tunable32 SCAN_SUSPEND_PROGRESS 0
	kill_if_running $pid
	is_pool_scrubbing $TESTPOOL && log_must zpool scrub -s $TESTPOOL
}

function do_test
{
	typeset stop_cmd=$1

	log_must zpool scrub $TESTPOOL
	log_bkgrnd zpool wait -t scrub $TESTPOOL
	pid=$!

	log_must sleep 3
	proc_must_exist $pid

	log_must eval "$stop_cmd"
	bkgrnd_proc_succeeded $pid
}

typeset pid

log_onexit cleanup

log_must set_tunable32 SCAN_SUSPEND_PROGRESS 1

do_test "zpool scrub -p $TESTPOOL"
do_test "zpool scrub -s $TESTPOOL"

log_pass "'zpool wait -t scrub' works when scrub is canceled."
