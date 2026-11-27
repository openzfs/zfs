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
# Copyright (c) 2026, John White.
#

. $STF_SUITE/tests/functional/async_mirror/async_mirror.kshlib

#
# DESCRIPTION:
#	A write that fails on the leader is not written to the lagging
#	child.  ZFS handles it as for a pool of one disk: while the leader
#	returns errors but still answers probes, the txg retries the write,
#	and when the errors stop the write completes.
#
# STRATEGY:
#	1. Create an async mirror and write a file that reaches both children.
#	2. Inject write errors on the leader and write a second file.
#	3. Start a pool sync, and wait until the leader reports write errors.
#	4. Verify that the sync has not completed and that the lagging child
#	   has no txg newer than the leader holds.
#	5. Remove the error injection and verify that the sync completes.
#	6. Export and import the pool, and verify both files.
#

verify_runnable "global"

function cleanup
{
	zinject -c all >/dev/null 2>&1
	[[ -n $sync_pid ]] && wait $sync_pid
	cleanup_async_mirror
	rm -f $TEST_BASE_DIR/async_mirror_late
}

typeset sync_pid=""
log_onexit cleanup
log_assert "A leader write error is retried, not written to the lagging child."

async_mirror_create_pool
async_mirror_enable
async_mirror_write_files saved 1
log_must wait_async_mirror_idle
typeset saved=$(async_mirror_digests saved 1)
typeset -i applied=$(get_async_mirror_stat applied_txg)

log_must dd if=/dev/urandom of=$TEST_BASE_DIR/async_mirror_late \
    bs=128k count=8
typeset late=$(xxh128digest $TEST_BASE_DIR/async_mirror_late)

typeset guid=$(get_leaf_guid $ASYNC_MIRROR_LEADER)
log_must zinject -d $guid -e io -T write $TESTPOOL
log_must cp $TEST_BASE_DIR/async_mirror_late /$TESTPOOL/late
zpool sync -l $TESTPOOL &
sync_pid=$!

typeset -i i
for (( i = 0; i < ASYNC_MIRROR_TIMEOUT; i++ )); do
	(( $(get_vdev_prop write_errors $TESTPOOL $ASYNC_MIRROR_LEADER) > 0 )) &&
	    break
	sleep 1
done
log_must test "$(get_vdev_prop write_errors $TESTPOOL \
    $ASYNC_MIRROR_LEADER)" -gt 0
log_must kill -0 $sync_pid
log_must test "$(get_async_mirror_stat applied_txg)" -le \
    "$(get_async_mirror_stat synced_txg)"
log_mustnot test "$(kstat_pool $TESTPOOL state)" == "SUSPENDED"

log_must zinject -c all
for (( i = 0; i < ASYNC_MIRROR_TIMEOUT; i++ )); do
	kill -0 $sync_pid 2>/dev/null || break
	sleep 1
done
log_mustnot kill -0 $sync_pid
log_must wait $sync_pid
sync_pid=""
log_must zpool clear $TESTPOOL
log_must test "$(get_async_mirror_stat active)" -eq 1
log_must wait_async_mirror_idle
log_must test "$(get_async_mirror_stat applied_txg)" -gt $applied

log_must zpool export $TESTPOOL
async_mirror_import
log_must test "$saved" == "$(async_mirror_digests saved 1)"
log_must test "$late" == "$(xxh128digest /$TESTPOOL/late)"

log_pass "A leader write error is retried, not written to the lagging child."
