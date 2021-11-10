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
# Copyright (c) 2020 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_wait/zpool_wait.kshlib

#
# DESCRIPTION:
# 'zpool wait' works when a trim operation is canceled.
#
# STRATEGY:
# 1. Create a pool.
# 2. Start trimming the vdev in the pool, setting the rate low enough that the
#    operation won't complete before the test finishes.
# 3. Start 'zpool wait'.
# 4. Wait a few seconds and then check that the wait process is actually
#    waiting.
# 5. Cancel the trim.
# 6. Check that the wait process returns reasonably promptly.
# 7. Repeat 3-7, except pause the trim instead of canceling it.
#

function cleanup
{
	kill_if_running $pid
	poolexists $TESTPOOL && destroy_pool $TESTPOOL
	[[ -d "$TESTDIR" ]] && log_must rm -r "$TESTDIR"
}

function do_test
{
	typeset stop_cmd=$1

	log_must zpool trim -r 1M $TESTPOOL "$FILE_VDEV"

	log_bkgrnd zpool wait -t trim $TESTPOOL
	pid=$!

	# Make sure that we are really waiting
	log_must sleep 3
	proc_must_exist $pid

	# Stop trimming and make sure process returns
	log_must eval "$stop_cmd"
	bkgrnd_proc_succeeded $pid
}

if is_freebsd; then
	log_unsupported "FreeBSD has no hole punching mechanism for the time being."
fi

typeset pid
typeset -r FILE_VDEV="$TESTDIR/file_vdev1"

log_onexit cleanup

log_must mkdir "$TESTDIR"
log_must truncate -s 10G "$FILE_VDEV"
log_must zpool create -f $TESTPOOL "$FILE_VDEV"

do_test "zpool trim -c $TESTPOOL $FILE_VDEV"
do_test "zpool trim -s $TESTPOOL $FILE_VDEV"

log_pass "'zpool wait' works when trim is stopped before completion."
