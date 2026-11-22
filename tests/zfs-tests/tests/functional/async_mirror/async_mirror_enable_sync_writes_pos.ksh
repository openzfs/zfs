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
#	The async mirror can be turned on while the pool has synchronous
#	writes.  The intent log allocates its blocks in an open transaction
#	group, so some of them are allocated before the switch on, in a
#	transaction group that syncs after it.  They stay on the leader, as
#	every other log block of this import: the record of that transaction
#	group does not list them.  If it did, the copy would have no block
#	pointer for them, and would copy their ranges as they are, while the
#	pool can already use those ranges again.
#
# STRATEGY:
#	1. Create a pool with a two-way mirror and no log device.  Set
#	   sync=always.  Stop the copy, and make it issue its blocks in many
#	   small passes.
#	2. Start writers of small synchronous writes.  Turn the async mirror
#	   on while they run, and stop them a few seconds later.
#	3. Write files in more transaction groups, so that the pool uses the
#	   ranges of the freed log blocks again.
#	4. Restart the copy, and wait until the lagging child has caught up.
#	   Verify that the copy did not fail, freeze or panic, and that it
#	   copied no range without a block pointer.
#	5. Export the pool and import the lagging child alone.  Verify the
#	   files, and that a scrub finds no error.
#

verify_runnable "global"

typeset stop=$TEST_BASE_DIR/async_mirror_enable_sync.stop.$$
typeset -i writers=4
# Four concurrent sync writers on a fresh 128 MiB file-vdev mirror can hit an
# upstream allocation failure (ENOSPC with the pool nearly empty) that suspends
# the pool; it also happens without an async mirror. Use larger vdevs.
ASYNC_MIRROR_VDEV_SIZE=256m

function cleanup
{
	touch $stop
	wait
	[[ -e $ASYNC_MIRROR_HIDDEN ]] && async_mirror_unhide_leader
	cleanup_async_mirror
	rm -f $stop
}

#
# Write a small file again and again until the stop file exists.  With
# sync=always, each write is a commit of the intent log.
#
# $1 path of the file
#
function sync_writer # file
{
	while [[ ! -e $stop ]]; do
		dd if=/dev/urandom of=$1 bs=8k count=8 conv=notrunc \
		    2>/dev/null || break
	done
}

log_onexit cleanup

log_assert "The async mirror can be turned on under synchronous writes."

async_mirror_create_pool
log_must zfs set sync=always $TESTPOOL
log_must async_mirror_set_tunable64 ASYNC_MIRROR_SORT_LIMIT 4096
async_mirror_suspend_copy 1

typeset -i i
for (( i = 0; i < writers; i++ )); do
	sync_writer /$TESTPOOL/sync.$i &
done
sleep 2
async_mirror_enable
sleep 2
log_must touch $stop
wait
log_must zfs set sync=standard $TESTPOOL

async_mirror_write_files file 6
log_must test "$(get_async_mirror_stat pending_txgs)" -gt 0
typeset digests="$(async_mirror_digests file 6)"

async_mirror_suspend_copy 0
log_must wait_async_mirror_idle
log_must test "$(get_async_mirror_stat failed)" == 0
log_must test "$(get_async_mirror_stat frozen)" == 0
log_must test "$(get_async_mirror_stat copy_rest_ranges)" == 0
log_must test "$(get_async_mirror_stat copy_rest_bytes)" == 0

log_must zpool export $TESTPOOL
async_mirror_import_lagging
log_must test "$digests" == "$(async_mirror_digests file 6)"
log_must zpool scrub -w $TESTPOOL
log_must check_pool_status $TESTPOOL errors "No known data errors"
log_must test "$(async_mirror_cksum_errors $ASYNC_MIRROR_LAGGING)" == 0
log_must zpool export $TESTPOOL
async_mirror_unhide_leader

log_pass "The async mirror can be turned on under synchronous writes."
