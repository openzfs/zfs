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

#
# DESCRIPTION:
# A pool that suspends while an fsync() waits for the ZIL's pending
# destroy must not leak the log block that the destroy keeps.
#
# STRATEGY:
# 1. Leave a log block in a file system's ZIL header, and unmount it.
# 2. Stop txgs from syncing on their own, and mount the file system:
#    zil_replay() destroys the ZIL but keeps its first block, in the
#    open txg.
# 3. "Pull" the disk and fsync() a file, so the pool suspends while
#    zil_create() waits for that txg.
# 4. Return the disk, clear the pool and verify with zdb that no space
#    has leaked.
#

verify_runnable "global"

typeset saved=""

function cleanup
{
	[[ -n "$saved" ]] && log_must restore_tunable TXG_TIMEOUT
	[[ -n "$sd" ]] && echo running > /sys/block/$sd/device/state
	poolexists $TESTPOOL && zpool clear $TESTPOOL
	destroy_pool $TESTPOOL
	unload_scsi_debug
}

log_onexit cleanup

log_assert "A suspend during ZIL creation does not leak the kept log block"

load_scsi_debug 256 1 1 1 '512b'
typeset sd=$(get_debug_device)

log_must zpool create -f -o failmode=wait $TESTPOOL $sd
log_must zfs create $TESTPOOL/fs

log_must dd if=/dev/urandom of=/$TESTPOOL/fs/file1 bs=4k count=16 \
    oflag=sync
log_must zpool sync $TESTPOOL
log_must zfs unmount $TESTPOOL/fs

log_must save_tunable TXG_TIMEOUT
saved=yes
log_must set_tunable32 TXG_TIMEOUT 3600
log_must zpool sync $TESTPOOL
log_must zfs mount $TESTPOOL/fs

log_must eval "echo offline > /sys/block/$sd/device/state"
dd if=/dev/urandom of=/$TESTPOOL/fs/file2 bs=4k count=1 conv=fsync &
typeset dd_pid=$!

log_note "waiting for pool to suspend"
typeset -i tries=30
until [[ $(kstat_pool $TESTPOOL state) == "SUSPENDED" ]] ; do
	if ((tries-- == 0)); then
		log_fail "pool didn't suspend"
	fi
	sleep 1
done

log_must eval "echo running > /sys/block/$sd/device/state"
log_must zpool clear $TESTPOOL
wait $dd_pid

log_must restore_tunable TXG_TIMEOUT
saved=""
log_must zpool sync $TESTPOOL
log_must zpool sync $TESTPOOL
typeset guid=$(get_pool_prop guid $TESTPOOL)
log_must zpool export $TESTPOOL

# By guid: other devices may carry stale labels of a pool by this name.
typeset out=$(zdb -e -p /dev -b $guid 2>&1)
log_note "$out"
if echo "$out" | grep -q "leaked space"; then
	log_fail "The pool leaked space"
fi
echo "$out" | grep -q "No leaks" || log_fail "zdb did not check for leaks"

log_must zpool import -d /dev $guid

log_pass "A suspend during ZIL creation does not leak the kept log block"
