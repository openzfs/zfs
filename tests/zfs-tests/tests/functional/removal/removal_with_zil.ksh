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

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/removal/removal.kshlib

#
# DESCRIPTION:
#	Removing a top-level vdev suspends the ZIL of every dataset
#	in the pool. For a file system whose ZIL is in use, that must
#	not wait for a txg to sync on the zfs_txg_timeout timer.
#
# STRATEGY:
#	1. Set zfs_txg_timeout to 300 seconds.
#	2. Write a file with O_SYNC, so the file system has a ZIL
#	   chain, and sync the pool so no txg is due before the timer.
#	3. Remove a vdev. The command must return within 60 seconds.
#	4. If it has not, sync the file system, which releases it,
#	   and fail.
#

verify_runnable "global"

function cleanup
{
	restore_tunable TXG_TIMEOUT
	default_cleanup_noexit
}

default_setup_noexit "$DISKS"
log_onexit cleanup

log_assert "Device removal does not wait for zfs_txg_timeout"

log_must save_tunable TXG_TIMEOUT
log_must set_tunable32 TXG_TIMEOUT 300

log_must dd if=/dev/urandom of=$TESTDIR/file bs=128k count=8 \
    oflag=sync
sync_pool $TESTPOOL

zpool remove $TESTPOOL $REMOVEDISK &
typeset pid=$!
typeset -i n=0
while kill -0 $pid 2>/dev/null && (( n < 60 )); do
	sleep 1
	(( n += 1 ))
done
if kill -0 $pid 2>/dev/null; then
	log_must sync
	log_must wait $pid
	log_fail "zpool remove still running after $n seconds"
fi
log_must wait $pid
log_note "zpool remove returned within $n seconds"

log_must restore_tunable TXG_TIMEOUT
log_must wait_for_removal $TESTPOOL
log_mustnot vdevs_in_pool $TESTPOOL $REMOVEDISK

log_pass "Device removal does not wait for zfs_txg_timeout"
