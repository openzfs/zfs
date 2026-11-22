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
#	A block pointer that the copy refuses in a transaction group that is
#	not the first of a sorted commit is reported once.  The walk of the
#	commit finds the refusal after the walk of the earlier transaction
#	groups.  The commit ends before the refused transaction group, and
#	the earlier ones are committed.  The copy must not walk the refused
#	transaction group again, because the block pointer does not change:
#	each walk would post another async_mirror_refused event.  The child
#	freezes at the transaction group before the refused one.
#
# STRATEGY:
#	1. Set the txg timeout to 5 minutes, so that only the test syncs txgs.
#	   Create a pool with a two-way mirror of 1 GiB, 8 KiB records and no
#	   compression, and turn the async mirror on.  Write file a, and wait
#	   until the lagging child has caught up.
#	2. Stop the copy.  Write three small files p, each in a txg of its
#	   own.  Write file b of 1000 blocks in a txg of its own, and then a
#	   small file q in a txg of its own.  The walk of a small txg
#	   adds far fewer than 200 block pointers, and the walk of b adds more
#	   than 1000.
#	3. Make the copy refuse the 200th block pointer of each walk, and
#	   restart the copy.  Only the walk of b reaches that block pointer,
#	   and b is the fourth txg of the commit.
#	4. Verify that the lagging child freezes, at a txg of the files p or
#	   later, and before b.  Verify that the pool posted exactly one
#	   async_mirror_refused event, that it names the txg of b, and one
#	   async_mirror_frozen event.
#	5. Verify that the pool still writes, and that the lagging child gets
#	   nothing of it: after an export its newest uberblock is the frozen
#	   txg.
#	6. Import the lagging child alone.  Verify files a and p, and that a
#	   scrub finds no error.
#
# On the code before the fix, the walk of b in the commit is refused and
# then walked again, twice: once as the first txg of the next commit, and
# once as a single txg.  Each walk posts the event, so the test finds 3.
#

verify_runnable "global"

typeset event_file=$TEST_BASE_DIR/async_mirror_refused_later_events.$$

function cleanup
{
	[[ -e $ASYNC_MIRROR_HIDDEN ]] && async_mirror_unhide_leader
	cleanup_async_mirror
	rm -f $event_file
}

#
# Write a file of two 8 KiB blocks.
#
function write_small # file
{
	log_must dd if=/dev/urandom of=$1 bs=8k count=2
}

#
# Print the digests of the files that the lagging child must hold.
#
function early_digests
{
	xxh128digest /$TESTPOOL/a
	async_mirror_digests p 3
}

log_onexit cleanup

log_assert "A refused block pointer in a later txg of a commit is reported" \
    "once."

log_must async_mirror_set_tunable32 TXG_TIMEOUT 300
#
# File b must be in one txg.  ZFS counts the dirty data of a txg 24 times
# against the space that is left, and forces a txg when that is more.  So
# 8 MiB in one txg need about 200 MiB of space: on a pool of 128 MiB the
# file is cut into several txgs, and the refused block pointer is in the
# first of them, not in the txg that the test notes for b.
#
ASYNC_MIRROR_VDEV_SIZE=1g
async_mirror_create_pool
log_must zfs set recordsize=8k $TESTPOOL
log_must zfs set compression=off $TESTPOOL
async_mirror_enable
write_small /$TESTPOOL/a
sync_pool $TESTPOOL
log_must wait_async_mirror_idle

async_mirror_suspend_copy 1
typeset -i i
for (( i = 0; i < 3; i++ )); do
	write_small /$TESTPOOL/p.$i
	sync_pool $TESTPOOL
done
log_must dd if=/dev/urandom of=/$TESTPOOL/b bs=8k count=1000
sync_pool $TESTPOOL
write_small /$TESTPOOL/q
sync_pool $TESTPOOL
# The txgs that wrote the data, not the synced txg after each sync.
typeset -i txg_p=$(async_mirror_file_birth p.2)
typeset -i txg_b=$(async_mirror_file_birth b)
typeset digests="$(early_digests)"
log_note "files p end in txg $txg_p, b is in txg $txg_b"
log_must test $txg_b -gt $txg_p
log_must test "$(get_async_mirror_stat pending_txgs)" -ge 5

log_must zpool events -c
log_must async_mirror_set_tunable32 ASYNC_MIRROR_INJECT_REFUSE 200
async_mirror_suspend_copy 0
log_must wait_async_mirror_frozen
log_must async_mirror_set_tunable32 ASYNC_MIRROR_INJECT_REFUSE 0

typeset -i frozen=$(get_async_mirror_stat frozen_txg)
log_note "the lagging child is frozen at txg $frozen"
log_must test $frozen -ge $txg_p
log_must test $frozen -lt $txg_b
log_must test "$(get_async_mirror_stat applied_txg)" == $frozen
log_must test "$(get_async_mirror_prop state)" == frozen

log_must eval "zpool events -v > $event_file"
typeset -i refused=$(async_mirror_event_count $event_file \
    async_mirror_refused)
log_note "$refused async_mirror_refused events"
log_must test $refused -eq 1
log_must test "$(async_mirror_event_count $event_file \
    async_mirror_frozen)" -eq 1
log_must grep -q 'the test suite asked for it' $event_file
typeset -i event_txg=$(async_mirror_event_txgs $event_file \
    async_mirror_refused)
log_note "the event names txg $event_txg"
log_must test $event_txg -eq $txg_b

# The pool goes on, and the frozen child gets nothing of it.
write_small /$TESTPOOL/c
sync_pool $TESTPOOL
log_must test $(get_async_mirror_pool_txg) -gt $txg_b
log_must zpool export $TESTPOOL
log_must test $(get_ring_max_txg $ASYNC_MIRROR_LAGGING) -eq $frozen

async_mirror_import_lagging
log_must test "$digests" == "$(early_digests)"
log_must zpool scrub -w $TESTPOOL
log_must check_pool_status $TESTPOOL errors "No known data errors"
log_must test "$(async_mirror_cksum_errors $ASYNC_MIRROR_LAGGING)" == 0
log_must zpool export $TESTPOOL
async_mirror_unhide_leader

log_pass "A refused block pointer in a later txg of a commit is reported" \
    "once."
