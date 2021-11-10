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
# 'zpool scrub -w' waits while scrub is in progress.
#
# STRATEGY:
# 1. Start a scrub with the -w flag.
# 2. Wait a few seconds and then check that the wait process is actually
#    waiting.
# 3. Stop the scrub, make sure that the command returns reasonably promptly.
#

function cleanup
{
	log_must set_tunable32 SCAN_SUSPEND_PROGRESS 0
	kill_if_running $pid
}

typeset pid

log_onexit cleanup

log_must set_tunable32 SCAN_SUSPEND_PROGRESS 1

log_bkgrnd zpool scrub -w $TESTPOOL
pid=$!

log_must sleep 3
proc_must_exist $pid

log_must zpool scrub -s $TESTPOOL
bkgrnd_proc_succeeded $pid

log_pass "'zpool scrub -w' works."
