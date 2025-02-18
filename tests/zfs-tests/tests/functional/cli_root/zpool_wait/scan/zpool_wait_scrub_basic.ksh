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
# 'zpool wait' works when waiting for a scrub to complete.
#
# STRATEGY:
# 1. Start a scrub.
# 2. Start 'zpool wait -t scrub'.
# 3. Monitor the waiting process to make sure it returns neither too soon nor
#    too late.
#

function cleanup
{
	remove_io_delay
	kill_if_running $pid
}

typeset pid

log_onexit cleanup

# Slow down scrub so that we actually have something to wait for.
add_io_delay $TESTPOOL

log_must zpool scrub $TESTPOOL
log_bkgrnd zpool wait -t scrub $TESTPOOL
pid=$!
check_while_waiting $pid "is_pool_scrubbing $TESTPOOL"

log_pass "'zpool wait -t scrub' works."
