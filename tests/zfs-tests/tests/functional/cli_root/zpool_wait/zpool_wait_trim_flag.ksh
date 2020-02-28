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
# -w flag for 'zpool trim' waits for trimming to complete for all and only those
# vdevs kicked off by that invocation.
#
# STRATEGY:
# 1. Create a pool with 3 vdevs.
# 2. Start trimming vdevs 1 and 2 with one invocation of 'zpool trim -w'
# 3. Start trimming vdev 3 with a second invocation of 'zpool trim -w'
# 4. Cancel the trim of vdev 1. Check that neither waiting process exits.
# 5. Cancel the trim of vdev 3. Check that only the second waiting process
#    exits.
# 6. Cancel the trim of vdev 2. Check that the first waiting process exits.
#

function cleanup
{
	kill_if_running $trim12_pid
	kill_if_running $trim3_pid
	poolexists $TESTPOOL && destroy_pool $TESTPOOL
	[[ -d "$TESTDIR" ]] && log_must rm -r "$TESTDIR"
}

if is_freebsd; then
	log_unsupported "FreeBSD has no hole punching mechanism for the time being."
fi

typeset trim12_pid trim3_pid
typeset -r VDEV1="$TESTDIR/file_vdev1"
typeset -r VDEV2="$TESTDIR/file_vdev2"
typeset -r VDEV3="$TESTDIR/file_vdev3"

log_onexit cleanup

log_must mkdir "$TESTDIR"
log_must truncate -s 10G "$VDEV1" "$VDEV2" "$VDEV3"
log_must zpool create -f $TESTPOOL "$VDEV1" "$VDEV2" "$VDEV3"

log_bkgrnd zpool trim -r 1M -w $TESTPOOL "$VDEV1" "$VDEV2"
trim12_pid=$!
log_bkgrnd zpool trim -r 1M -w $TESTPOOL "$VDEV3"
trim3_pid=$!

# Make sure that we are really waiting
log_must sleep 3
proc_must_exist $trim12_pid
proc_must_exist $trim3_pid

#
# Cancel trim of one of disks started by trim12, make sure neither
# process exits
#
log_must zpool trim -c $TESTPOOL "$VDEV1"
proc_must_exist $trim12_pid
proc_must_exist $trim3_pid

#
# Cancel trim started by trim3, make sure that process exits, but
# trim12 doesn't
#
log_must zpool trim -c $TESTPOOL "$VDEV3"
proc_must_exist $trim12_pid
bkgrnd_proc_succeeded $trim3_pid

# Cancel last trim started by trim12, make sure it returns.
log_must zpool trim -c $TESTPOOL "$VDEV2"
bkgrnd_proc_succeeded $trim12_pid

log_pass "'zpool trim -w' works."
