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
# Copyright (c) 2017 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/slog/slog.kshlib

#
# DESCRIPTION:
#	Concurrent sync writes with log offline/online works.
#
# STRATEGY:
#	1. Configure "zfs_commit_timeout_pct"
#	2. Create pool with a log device.
#	3. Concurrently do the following:
#	   3.1. Perform 8K sync writes
#	   3.2. Perform log offline/online commands
#	4. Loop to test with growing "zfs_commit_timeout_pct" values.
#

verify_runnable "global"

command -v fio > /dev/null || log_unsupported "fio missing"

function cleanup
{
	#
	# Wait for any of the writes and/or zpool commands that were
	# kicked off in the background to complete. On failure, we may
	# enter this function without previously waiting for them.
	#
	wait

	set_tunable64 COMMIT_TIMEOUT_PCT $ORIG_TIMEOUT

	poolexists $TESTPOOL && zpool destroy -f $TESTPOOL
}

typeset ORIG_TIMEOUT=$(get_tunable COMMIT_TIMEOUT_PCT)
log_onexit cleanup
log_must setup

for PCT in 0 1 2 4 8 16 32 64 128 256 512 1024; do
	log_must set_tunable64 COMMIT_TIMEOUT_PCT $PCT

	log_must zpool create $TESTPOOL $VDEV log $SDEV

	for i in {1..10}; do
		log_must fio --rw write --sync 1 --directory "/$TESTPOOL" \
		    --bs 8K --size 8K --name slog-test
	done &

	for i in {1..10}; do
		log_must zpool offline $TESTPOOL $SDEV
		log_must verify_slog_device $TESTPOOL $SDEV 'OFFLINE'
		log_must zpool online $TESTPOOL $SDEV
		log_must verify_slog_device $TESTPOOL $SDEV 'ONLINE'
	done &

	wait

	log_must zpool destroy -f $TESTPOOL
done

log_pass "Concurrent writes with slog offline/online works."
