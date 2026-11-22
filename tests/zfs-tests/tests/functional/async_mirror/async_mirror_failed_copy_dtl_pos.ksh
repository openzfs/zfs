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
#	After a failed replay, the DTL of the lagging child reaches every
#	txg that was open at the failure.  An fsync write with
#	logbias=throughput writes its block in open context, with the
#	birth txg of the open txg, and the write to the lagging child is
#	dropped.  A release can be queued in an older txg, which closed
#	before that write.  The DTL is set when the newest txg that was
#	open at the failure syncs, and the resilver must copy the block.
#
# STRATEGY:
#	1. Create a pool with a two-way mirror and turn the async mirror on.  Set
#	   logbias=throughput, and make the intent log with one fsync write.
#	   Make txgs sync only when asked to.
#	2. Stop the copy.  Write file j and sync.  Restart the copy, and
#	   wait until it has copied j: the copy queues a release in the
#	   open txg Q.
#	3. Stop the copy.  Write file k in Q, and sync Q: Q closes before
#	   the next write, and the lagging child has not got it.
#	4. Write file s with fsync, in txg Q+1.
#	5. Make each write to the lagging child fail, and restart the copy:
#	   the copy of Q fails while Q+1 is open.  Clear the faults.
#	6. Wait for the resilver.  Verify that a scrub finds no checksum
#	   error on the lagging child, and that the lagging child alone
#	   holds file s.
#

verify_runnable "global"

function cleanup
{
	zinject -c all > /dev/null 2>&1
	[[ -e $ASYNC_MIRROR_HIDDEN ]] && async_mirror_unhide_leader
	cleanup_async_mirror
}

log_onexit cleanup

async_mirror_create_pool
async_mirror_enable
log_must zfs set logbias=throughput recordsize=128k $TESTPOOL
log_must dd if=/dev/urandom of=/$TESTPOOL/pre bs=128k count=1 oflag=sync
async_mirror_write_files a 1
log_must wait_async_mirror_idle

log_must async_mirror_set_tunable32 TXG_TIMEOUT 600
async_mirror_suspend_copy 1
async_mirror_write /$TESTPOOL/j 1
sync_pool $TESTPOOL
typeset -i applied=$(get_async_mirror_stat applied_txg)
log_must test "$(get_async_mirror_stat pending_txgs)" -ge 1
async_mirror_suspend_copy 0
log_must wait_async_mirror_applied $((applied + 1))

async_mirror_suspend_copy 1
async_mirror_write /$TESTPOOL/k 64
sync_pool $TESTPOOL
log_must test "$(get_async_mirror_stat pending_txgs)" -ge 1
log_must dd if=/dev/urandom of=/$TESTPOOL/s bs=128k count=1 oflag=sync
typeset digest=$(xxh128digest /$TESTPOOL/s)

log_must zinject -d $(get_leaf_guid $ASYNC_MIRROR_LAGGING) -e io -T write \
    $TESTPOOL
async_mirror_suspend_copy 0
log_must wait_async_mirror_failed
log_must zinject -c all
log_must wait_async_mirror_resilvered
log_must zpool scrub -w $TESTPOOL
typeset -i ck=$(async_mirror_cksum_errors $ASYNC_MIRROR_LAGGING)
log_note "checksum errors on the lagging child: $ck"
log_must test $ck -eq 0
log_must zpool export $TESTPOOL

log_must mv $ASYNC_MIRROR_LEADER $ASYNC_MIRROR_HIDDEN
log_must zpool import -o readonly=on -d $ASYNC_MIRROR_DIR $TESTPOOL
log_must test "$digest" == "$(xxh128digest /$TESTPOOL/s)"
log_must check_pool_status $TESTPOOL errors "No known data errors"
log_must zpool export $TESTPOOL
async_mirror_unhide_leader

log_pass "After a failed replay, the DTL reaches every txg open at the " \
    "failure."
