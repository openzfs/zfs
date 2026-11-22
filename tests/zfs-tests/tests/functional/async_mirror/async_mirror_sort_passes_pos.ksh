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
#	The copy to the lagging child sorts the blocks of all the
#	transaction groups of one commit by their offset, and issues them
#	when they use zfs_vdev_async_mirror_sort_limit bytes of memory.  With
#	a small limit, one commit makes many passes, and the I/O of the
#	blocks of one transaction group runs while the copy walks the next
#	ones.  The lagging child still gets every block of every transaction
#	group before the uberblock of the commit.
#
# STRATEGY:
#	1. Create a pool with a two-way mirror and turn the async mirror on.
#	   Use blocks of 8 KiB, so that a transaction group has many blocks.
#	2. Set the limit to 4 KiB, the memory of about 13 blocks.  Stop the
#	   copy.  In each of 6 transaction groups, write a file and write
#	   every second block of the file before it again, so that the
#	   transaction groups free and allocate between each other's blocks.
#	3. Restart the copy, and wait until the lagging child has caught up.
#	   Verify that the copy neither failed nor froze, lost no block, and
#	   copied no range without a block pointer.
#	4. Export the pool and import the lagging child alone.  Verify the
#	   files, and that a scrub finds no error.
#

verify_runnable "global"

typeset src=$TEST_BASE_DIR/async_mirror_sort_passes.src.$$
typeset -i files=6
typeset -i blocks=256

function cleanup
{
	[[ -e $ASYNC_MIRROR_HIDDEN ]] && async_mirror_unhide_leader
	cleanup_async_mirror
	rm -f $src
}

#
# Print the digests of the files of the test.
#
function file_digests
{
	typeset -i i

	for (( i = 0; i < files; i++ )); do
		xxh128digest /$TESTPOOL/fs/file.$i
	done
}

log_onexit cleanup

log_assert "A commit of many sorted passes copies every block."

async_mirror_create_pool
async_mirror_enable
log_must zfs create -o recordsize=8k -o compression=off $TESTPOOL/fs
log_must wait_async_mirror_idle

log_must async_mirror_set_tunable64 ASYNC_MIRROR_SORT_LIMIT 4096
async_mirror_suspend_copy 1
typeset -i i
for (( i = 0; i < files; i++ )); do
	log_must dd if=/dev/urandom of=$src bs=8k count=$blocks
	log_must dd if=$src of=/$TESTPOOL/fs/file.$i bs=8k
	if (( i > 0 )); then
		log_must stride_dd -i $src -o /$TESTPOOL/fs/file.$(( i - 1 )) \
		    -b 8192 -c $(( blocks / 2 )) -s 2
	fi
	sync_pool $TESTPOOL
done
log_must test "$(get_async_mirror_stat pending_txgs)" -ge $files
typeset digests="$(file_digests)"

async_mirror_suspend_copy 0
log_must wait_async_mirror_idle
log_must test "$(get_async_mirror_stat failed)" == 0
log_must test "$(get_async_mirror_stat frozen)" == 0
log_must test "$(get_async_mirror_stat data_errors)" == 0
log_must test "$(get_async_mirror_stat copy_rest_ranges)" == 0

log_must zpool export $TESTPOOL
async_mirror_import_lagging
log_must test "$digests" == "$(file_digests)"
log_must zpool scrub -w $TESTPOOL
log_must check_pool_status $TESTPOOL errors "No known data errors"
log_must test "$(async_mirror_cksum_errors $ASYNC_MIRROR_LAGGING)" == 0
log_must zpool export $TESTPOOL
async_mirror_unhide_leader

log_pass "A commit of many sorted passes copies every block."
