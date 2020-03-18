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
. $STF_SUITE/tests/functional/cli_root/zfs_wait/zfs_wait.kshlib

#
# DESCRIPTION:
# 'zfs wait' works when waiting for checkpoint discard to complete.
#
# STRATEGY:
# 1. Create a file
# 2. Open a file descriptor pointing to that file.
# 3. Delete the file.
# 4. Start a background process waiting for the delete queue to empty.
# 5. Verify that the command doesn't return immediately.
# 6. Close the open file descriptor.
# 7. Verify that the command returns soon after the descriptor is closed.
#

function cleanup
{
	kill_if_running $pid
	exec 3<&-
}


typeset -r TESTFILE="/$TESTPOOL/testfile"
typeset pid

log_onexit cleanup

log_must touch $TESTFILE
exec 3<> $TESTFILE
log_must rm $TESTFILE
log_bkgrnd zfs wait -t deleteq $TESTPOOL
pid=$!
proc_must_exist $pid

exec 3<&-
log_must sleep 0.5
bkgrnd_proc_succeeded $pid

log_pass "'zfs wait -t discard' works."
