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
#	With a log device, the lagging child alone still imports at its own
#	txg.  The pool does not write a newer uberblock to the log while the
#	lagging child holds an older txg.
#
# STRATEGY:
#	1. Create a pool with a two-way mirror and a log device, and turn the
#	   async mirror on.  Write files a and wait
#	   until the lagging child has caught up.
#	2. Stop the copy and write file b.  Export and import twice to commit
#	   pool configuration changes while the child is behind.  Export.
#	3. Verify that the newest uberblock of the log device is not newer
#	   than that of the lagging child.
#	4. Hide the leader and inspect the raw lagging child with zdb.
#	5. Import the lagging child read-write.
#	   Verify the same data and that a scrub finds no error.
#

verify_runnable "global"

function cleanup
{
	[[ -e $ASYNC_MIRROR_HIDDEN ]] && async_mirror_unhide_leader
	cleanup_async_mirror
}

log_onexit cleanup

log_must truncate -s $ASYNC_MIRROR_VDEV_SIZE $ASYNC_MIRROR_LEADER \
    $ASYNC_MIRROR_LAGGING $ASYNC_MIRROR_EXTRA
log_must zpool create -f $TESTPOOL mirror $ASYNC_MIRROR_LEADER \
    $ASYNC_MIRROR_LAGGING log $ASYNC_MIRROR_EXTRA
async_mirror_enable
async_mirror_write_files a 2
typeset digests="$(async_mirror_digests a 2)"
log_must wait_async_mirror_idle

async_mirror_suspend_copy 1
async_mirror_write /$TESTPOOL/b 16
sync_pool $TESTPOOL
log_must zpool export $TESTPOOL
async_mirror_import
log_must zpool export $TESTPOOL
async_mirror_import
log_must zpool export $TESTPOOL

typeset -i lag=$(get_ring_max_txg $ASYNC_MIRROR_LAGGING)
typeset -i logdev=$(get_ring_max_txg $ASYNC_MIRROR_EXTRA)
typeset -i lead=$(get_ring_max_txg $ASYNC_MIRROR_LEADER)
log_note "newest uberblocks: leader $lead, lagging $lag, log $logdev"
log_must test $lead -gt $lag
log_must test $logdev -le $lag

log_must mv $ASYNC_MIRROR_LEADER $ASYNC_MIRROR_HIDDEN
log_must zdb -e -p $ASYNC_MIRROR_DIR $TESTPOOL

log_must zpool import -d $ASYNC_MIRROR_DIR $TESTPOOL
log_must test "$digests" == "$(async_mirror_digests a 2)"
log_mustnot test -e /$TESTPOOL/b
log_must zpool scrub -w $TESTPOOL
log_must check_pool_status $TESTPOOL errors "No known data errors"
log_must zpool export $TESTPOOL
async_mirror_unhide_leader

log_pass "With a log device, the lagging child alone imports at its txg."
