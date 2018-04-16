#! /bin/ksh -p
#
# CDDL HEADER START
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
# CDDL HEADER END
#

#
# Copyright (c) 2017 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/removal/removal.kshlib

#
# DESCRIPTION:
#
# When a pool has an ongoing removal and it is exported ZFS
# suspends the removal thread beforehand. This test ensures
# that ZFS restarts the removal thread if the export fails
# for some reason.
#
# STRATEGY:
#
# 1. Create a pool with one vdev and do some writes on it.
# 2. Add a new vdev to the pool and start the removal of
#    the first vdev.
# 3. Inject a fault in the pool and attempt to export (it
#    should fail).
# 4. After the export fails ensure that the removal thread
#    was restarted (i.e. the svr_thread field in the spa
#    should be non-zero).
#


function cleanup
{
	log_must zinject -c all
	default_cleanup_noexit
}

function ensure_thread_running # spa_address
{
	if is_linux; then
		typeset TRIES=0
		typeset THREAD_PID
		while [[ $TRIES -lt 10 ]]; do
			THREAD_PID=$(pgrep spa_vdev_remove)
			[[ "$THREAD_PID" ]] && break
			sleep 0.1
			(( TRIES = TRIES + 1 ))
		done
		[[ "$THREAD_PID" ]] || \
		    log_fail "removal thread is not running TRIES=$TRIES THREAD_PID=$THREAD_PID"
	else
		#
		# Try to get the address of the removal thread.
		#
		typeset THREAD_ADDR=$(mdb -ke "$1::print \
		    spa_t spa_vdev_removal->svr_thread" | awk "{print \$3}")

		#
		# if address is NULL it means that the thread is
		# not running.
		#
		[[ "$THREAD_ADDR" = 0 ]] && \
		    log_fail "removal thread is not running"
	fi

	return 0
}

log_onexit cleanup

#
# Create pool with one disk.
#
log_must default_setup_noexit "$REMOVEDISK"

#
# Save address of SPA in memory so you can check with mdb
# if the removal thread is running.
#
is_linux || typeset SPA_ADDR=$(mdb -ke "::spa" | awk "/$TESTPOOL/ {print \$1}")

#
# Turn off compression to raise capacity as much as possible
# for the little time that this test runs.
#
log_must zfs set compression=off $TESTPOOL/$TESTFS

#
# Write some data that will be evacuated from the device when
# we start the removal.
#
log_must dd if=/dev/urandom of=$TESTDIR/$TESTFILE0 bs=64M count=32

#
# Add second device where all the data will be evacuated.
#
log_must zpool add -f $TESTPOOL $NOTREMOVEDISK

#
# Start removal.
#
log_must zpool remove $TESTPOOL $REMOVEDISK

#
# Inject an error so export fails after having just suspended
# the removal thread. [spa_inject_ref gets incremented]
#
log_must zinject -d $REMOVEDISK -D 10:1 $TESTPOOL

log_must ensure_thread_running $SPA_ADDR

#
# Because of the above error export should fail.
#
log_mustnot zpool export $TESTPOOL

log_must ensure_thread_running $SPA_ADDR

wait_for_removal $TESTPOOL

log_pass "Device removal thread resumes after failed export"
