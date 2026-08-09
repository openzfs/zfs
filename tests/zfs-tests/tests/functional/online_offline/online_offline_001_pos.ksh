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
# https://opensource.org/license/CDDL-1.0.
#

#
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/online_offline/online_offline.cfg

#
# DESCRIPTION:
# Turning a disk offline and back online during I/O completes.
#
# STRATEGY:
# 1. Create a mirror and start some random I/O
# 2. For each disk in the mirror, set it offline and online
# 3. Verify the integrity of the file system and the resilvering.
#

verify_runnable "global"
log_onexit cleanup

DISKLIST=$(get_disklist $TESTPOOL)

function cleanup
{
	kill $killpid >/dev/null 2>&1

	#
	# Ensure we don't leave disks in the offline state
	#
	for disk in $DISKLIST; do
		log_must zpool online $TESTPOOL $disk
		log_must check_state $TESTPOOL $disk "online"
	done
	sleep 1 # Delay for resilver to start
	log_must zpool wait -t resilver $TESTPOOL

	[[ -e $TESTDIR ]] && log_must rm -rf $TESTDIR/*
}

log_assert "Turning a disk offline and back online during I/O completes."

file_trunc -f $((64 * 1024 * 1024)) -b 8192 -c 0 -r $TESTDIR/$TESTFILE1 &
typeset killpid="$! "

for disk in $DISKLIST; do
	for i in 'do_offline' 'do_offline_while_already_offline'; do
		log_must zpool offline $TESTPOOL $disk
		log_must check_state $TESTPOOL $disk "offline"
	done

	log_must zpool online $TESTPOOL $disk
	log_must check_state $TESTPOOL $disk "online"
	sleep 1 # Delay for resilver to start
	log_must zpool wait -t resilver $TESTPOOL
done

log_must kill $killpid
sync_all_pools
log_must sync

typeset dir=$(get_device_dir $DISKS)
verify_filesys "$TESTPOOL" "$TESTPOOL/$TESTFS" "$dir"

log_pass
