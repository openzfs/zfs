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
# Copyright (c) 2026 by OpenZFS contributors.
#

#
# DESCRIPTION:
#	Ensure zpool clear accepts an MMP timestamp written by this host.
#
# STRATEGY:
#	1. Create a multihost mirror and allow MMP to advance its timestamp.
#	2. Suspend the pool by delaying uberblock writes.
#	3. Remove the delay and verify zpool clear resumes the pool.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/mmp/mmp.cfg
. $STF_SUITE/tests/functional/mmp/mmp.kshlib

verify_runnable "global"

MMP_TEST_POOL=mmpclear
MMP_TEST_DIR=$TEST_BASE_DIR/mmp-clear-timestamp
SUSPEND_TIMEOUT=30
CLEAR_TIMEOUT=60
SKIP_POOL_CLEANUP=0

function cleanup
{
	if (( SKIP_POOL_CLEANUP )); then
		log_note "Skipping pool cleanup after a stuck clear; discard VM"
		set_tunable64 MULTIHOST_INTERVAL $MMP_INTERVAL_DEFAULT
		set_tunable64 MULTIHOST_FAIL_INTERVALS \
		    $MMP_FAIL_INTERVALS_DEFAULT
		mmp_clear_hostid
		return
	fi

	log_must zinject -c all
	mmp_clear_suspended $MMP_TEST_POOL
	poolexists $MMP_TEST_POOL && destroy_pool $MMP_TEST_POOL
	log_must rm -rf $MMP_TEST_DIR
	log_must mmp_clear_hostid
	log_must set_tunable64 MULTIHOST_INTERVAL $MMP_INTERVAL_DEFAULT
	log_must set_tunable64 MULTIHOST_FAIL_INTERVALS \
	    $MMP_FAIL_INTERVALS_DEFAULT
}

function process_state # pid
{
	sed 's/.*) //' /proc/$1/stat 2>/dev/null | cut -d' ' -f1
}

log_assert "zpool clear accepts a local MMP timestamp update"
log_onexit cleanup

log_must set_tunable64 MULTIHOST_INTERVAL $MMP_INTERVAL_MIN
log_must set_tunable64 MULTIHOST_FAIL_INTERVALS $MMP_FAIL_INTERVALS_MIN
log_must mkdir -p $MMP_TEST_DIR
log_must truncate -s $MINVDEVSIZE $MMP_TEST_DIR/vdev1 \
	$MMP_TEST_DIR/vdev2
log_must mmp_clear_hostid
log_must mmp_set_hostid $HOSTID1
log_must zpool create -f -o failmode=wait \
	-o cachefile=$MMP_TEST_DIR/zpool.cache $MMP_TEST_POOL mirror \
	$MMP_TEST_DIR/vdev1 $MMP_TEST_DIR/vdev2
log_must zpool set multihost=on $MMP_TEST_POOL

# With the short interval, this allows MMP to write a newer timestamp while
# the synced txg remains unchanged.
log_must sleep 3
log_must zinject -D 10000:1 -T write -L uber -d $MMP_TEST_DIR/vdev1 \
	$MMP_TEST_POOL
log_must zinject -D 10000:1 -T write -L uber -d $MMP_TEST_DIR/vdev2 \
	$MMP_TEST_POOL

typeset suspended=0
for ((i = 0; i < SUSPEND_TIMEOUT; i++)); do
	state=$(cat /proc/spl/kstat/zfs/$MMP_TEST_POOL/state 2>/dev/null)
	if [[ $state == "SUSPENDED" ]]; then
		suspended=1
		break
	fi
	sleep 1
done

if (( ! suspended )); then
	log_fail "$MMP_TEST_POOL did not suspend after delayed uberblock writes"
fi
SKIP_POOL_CLEANUP=1

log_must zinject -c all
zpool clear $MMP_TEST_POOL &
clear_pid=$!
for ((i = 0; i < CLEAR_TIMEOUT; i++)); do
	state=$(process_state $clear_pid)
	[[ -z $state || $state == "Z" ]] && break
	sleep 1
done

state=$(process_state $clear_pid)
if [[ -n $state && $state != "Z" ]]; then
	log_fail "zpool clear did not return within ${CLEAR_TIMEOUT}s (state=$state)"
fi
SKIP_POOL_CLEANUP=0

if ! wait $clear_pid; then
	log_fail "zpool clear rejected the local MMP timestamp"
fi
SKIP_POOL_CLEANUP=0

log_pass "zpool clear accepts a local MMP timestamp update"
