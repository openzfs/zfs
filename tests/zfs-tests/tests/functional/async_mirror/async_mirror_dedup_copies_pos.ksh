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
#	The copy to the lagging child reaches a copy that a dedup write adds
#	to an older block.  With the copies property raised, a write of data
#	that the dedup table holds allocates only the new copy.  The block
#	pointer keeps the physical birth of the older block, so the copy
#	cannot find the new copy by its birth: it finds it in the record of
#	the transaction group.
#
# STRATEGY:
#	1. Create a pool with a two-way mirror, turn the async mirror on, and
#	   create a dataset with dedup.  The pool has the fast_dedup feature:
#	   without it, each number of copies has a dedup entry of its own,
#	   and no copy is added to an older block.
#	2. Write a file, and wait until the lagging child has it.
#	3. Set copies=2 and write the same data to a second file.  Verify
#	   with zdb that its first block has two copies and a physical birth
#	   before its logical birth.
#	4. Wait for the copy, and verify no failure and no freeze.  Export.
#	5. Import the lagging child alone and scrub it.  Verify that it has
#	   no checksum error, so each new copy is there, and that both files
#	   are intact.
#

verify_runnable "global"

typeset src=$TEST_BASE_DIR/async_mirror_dedup_copies.src

function cleanup
{
	cleanup_async_mirror
	rm -f $src
}

#
# Print the number of copies of the first data block of an object, and its
# logical and physical birth.  zdb prints each copy of a block only from
# six -d options; with five it prints the first copy alone.
#
# $1 dataset
# $2 object
#
function first_block # dataset object
{
	zdb -dddddd $1 $2 | awk '$2 == "L0" {
		n = 0
		for (i = 3; i <= NF; i++) {
			if ($i ~ /^[0-9]+:[0-9a-f]+:[0-9a-f]+G?$/)
				n++
			if ($i ~ /^B=/)
				split(substr($i, 3), b, "/")
		}
		print n, b[1], b[2]
		exit
	}'
}

log_onexit cleanup

log_assert "The copy reaches a copy that a dedup write adds to an older block."

async_mirror_create_pool
async_mirror_enable
log_must test "$(get_pool_prop feature@fast_dedup $TESTPOOL)" != disabled
log_must zfs create -o dedup=on -o checksum=sha256 -o compression=off \
    -o recordsize=128k $TESTPOOL/dedup
log_must dd if=/dev/urandom of=$src bs=128k count=8
log_must cp $src /$TESTPOOL/dedup/one
sync_pool $TESTPOOL
log_must wait_async_mirror_idle

log_must zfs set copies=2 $TESTPOOL/dedup
log_must cp $src /$TESTPOOL/dedup/two
sync_pool $TESTPOOL
typeset obj=$(ls -i /$TESTPOOL/dedup/two | awk '{print $1}')
set -A blk $(first_block $TESTPOOL/dedup $obj)
log_note "first block of two: ${blk[0]} copies, birth ${blk[1]}/${blk[2]}"
log_must test "${blk[0]}" == 2
log_must test "${blk[2]}" -lt "${blk[1]}"

log_must wait_async_mirror_idle
log_must test "$(get_async_mirror_stat failed)" == 0
log_must test "$(get_async_mirror_stat frozen)" == 0
log_must zpool export $TESTPOOL

async_mirror_import_lagging
log_must zpool scrub -w $TESTPOOL
log_must check_pool_status $TESTPOOL errors "No known data errors"
log_must test "$(async_mirror_cksum_errors $ASYNC_MIRROR_LAGGING)" == 0
log_must cmp $src /$TESTPOOL/dedup/one
log_must cmp $src /$TESTPOOL/dedup/two
log_must zpool export $TESTPOOL
async_mirror_unhide_leader

log_pass "The copy reaches a copy that a dedup write adds to an older block."
