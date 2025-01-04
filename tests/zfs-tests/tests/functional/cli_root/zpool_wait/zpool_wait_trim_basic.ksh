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
# Copyright (c) 2020 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_wait/zpool_wait.kshlib

#
# DESCRIPTION:
# 'zpool wait' works when waiting for devices to finish being trimmed
#
# STRATEGY:
# 1. Create a pool.
# 2. Start trimming the vdev in the pool, making sure the rate is slow enough
#    that the trim can be observed.
# 3. Start 'zpool wait'.
# 4. Monitor the waiting process to make sure it returns neither too soon nor
#    too late.
#

function cleanup
{
	kill_if_running $pid
	poolexists $TESTPOOL && destroy_pool $TESTPOOL

	[[ -d "$TESTDIR" ]] && log_must rm -r "$TESTDIR"
}

# Check whether any vdevs in given pool are being trimmed
function trim_in_progress
{
	typeset pool="$1"
	zpool status -t "$pool" | grep "trimmed, started"
}

if is_freebsd; then
	log_unsupported "FreeBSD has no hole punching mechanism for the time being."
fi

typeset -r FILE_VDEV="$TESTDIR/file_vdev"
typeset pid

log_onexit cleanup

log_must mkdir "$TESTDIR"
log_must truncate -s 10G "$FILE_VDEV"
log_must zpool create -f $TESTPOOL "$FILE_VDEV"

log_must zpool trim -r 2G $TESTPOOL "$FILE_VDEV"

log_bkgrnd zpool wait -t trim $TESTPOOL
pid=$!

check_while_waiting $pid "trim_in_progress $TESTPOOL"

log_pass "'zpool wait -t trim' works."
