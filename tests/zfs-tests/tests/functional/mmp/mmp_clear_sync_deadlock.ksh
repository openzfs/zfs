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

# DESCRIPTION:
#	Ensure zpool clear can check MMP activity while txg_sync is suspended.
#
# STRATEGY:
#	1. Create a multihost mirror and start synchronous writes.
#	2. Fail all uberblock writes until the pool suspends in txg_sync.
#	3. Remove the injection and verify zpool clear returns.

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/mmp/mmp.cfg
. $STF_SUITE/tests/functional/mmp/mmp.kshlib

verify_runnable "global"

SUSPEND_TIMEOUT=90
CLEAR_TIMEOUT=60
WRITER_PIDS=""
SKIP_POOL_CLEANUP=0

function cleanup
{
	if (( SKIP_POOL_CLEANUP )); then
		log_note "Skipping pool cleanup; discard VM after a stuck clear"
		return
	fi

	log_must zinject -c all
	for pid in $WRITER_PIDS; do
		kill $pid 2>/dev/null
	done
	for pid in $WRITER_PIDS; do
		wait $pid 2>/dev/null
	done

	mmp_clear_suspended $MMP_POOL
	poolexists $MMP_POOL && destroy_pool $MMP_POOL
	log_must rm -rf $MMP_DIR
	log_must mmp_clear_hostid
}

function process_state # pid
{
	sed 's/.*) //' /proc/$1/stat 2>/dev/null | cut -d' ' -f1
}

log_assert "zpool clear does not deadlock with a suspended txg_sync"
log_onexit cleanup

log_must mkdir -p $MMP_DIR
log_must truncate -s $MINVDEVSIZE $MMP_DIR/vdev1 $MMP_DIR/vdev2
log_must mmp_clear_hostid
log_must mmp_set_hostid $HOSTID1
log_must zpool create -f -o failmode=wait -o cachefile=$MMP_CACHE \
	$MMP_POOL mirror $MMP_DIR/vdev1 $MMP_DIR/vdev2
log_must zfs create -o mountpoint=$MMP_DIR/mnt $MMP_POOL/data

for i in 0 1 2 3; do
	(
		while :; do
			dd if=/dev/zero of=$MMP_DIR/mnt/write.$i bs=128k \
			    count=64 oflag=sync conv=notrunc \
			    >/dev/null 2>&1 || break
		done
	) &
	WRITER_PIDS="$WRITER_PIDS $!"
done

# Keep synchronous writes in flight before enabling MMP, then inject failures
# immediately.  A settling delay can let MMP advance the uberblock timestamp
# and exercise the separate remote-activity rejection in #19033 instead.
sleep 4
log_must zpool set multihost=on $MMP_POOL
log_must zinject -e io -f 100 -T write -L uber -d $MMP_DIR/vdev1 \
	$MMP_POOL
log_must zinject -e io -f 100 -T write -L uber -d $MMP_DIR/vdev2 \
	$MMP_POOL

typeset suspended=0
for ((i = 0; i < SUSPEND_TIMEOUT; i++)); do
	state=$(cat /proc/spl/kstat/zfs/$MMP_POOL/state 2>/dev/null)
	if [[ $state == "SUSPENDED" ]]; then
		suspended=1
		break
	fi
	sleep 1
done

if (( ! suspended )); then
	log_fail "$MMP_POOL did not suspend after uberblock write failures"
fi
SKIP_POOL_CLEANUP=1

# The injected errors must be gone before zpool clear retries the sync.
log_must zinject -c all

zpool clear $MMP_POOL &
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
	log_fail "zpool clear failed after the suspended sync was resumed"
fi
SKIP_POOL_CLEANUP=0

log_pass "zpool clear returns while txg_sync waits for resume"
