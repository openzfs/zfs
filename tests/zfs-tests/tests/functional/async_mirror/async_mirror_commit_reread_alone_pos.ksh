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
#	A block that cannot be read in a later transaction group of a sorted
#	commit ends the commit before that transaction group.  The
#	transaction group of that block is then copied alone, with its tries.
#	The copy must not sort it again as the first transaction group of
#	another commit: that commit would fail at the same block, and cost
#	another walk of the transaction group, with its reads, before the
#	tries start.
#
# STRATEGY:
#	1. Create a pool with a two-way mirror and turn the async mirror on.
#	   Write files a, and wait until the lagging child has caught up.
#	2. With the copy stopped, write three files p, then file b, then
#	   three files q, each in a transaction group of its own.  Export
#	   and import, so that the ARC does not hold b.  Inject read errors
#	   on the data of b.
#	3. Restart the copy, and wait for two tries after a read error.  The
#	   tries are 3 seconds apart.
#	4. Verify that the lagging child has passed the files p and not b,
#	   and that no block is deferred yet.  Verify that the debug log has
#	   one line "the commit ends before this txg" for the txg of b since
#	   the restart of the copy.  A second line means that the txg was
#	   sorted again before its tries.
#	5. Clear the injection.  Wait until the lagging child has caught up.
#	   Verify that the copy neither failed nor froze, that no data error
#	   was counted, that no block was deferred, that the debug log still
#	   has one such line, and that one async_mirror_retry event was
#	   posted: one burst of tries.
#	6. Export the pool and import the lagging child alone.  Verify every
#	   file, and that a scrub finds no error.
#
# On the code before the fix, the first commit ends before b, the next
# commit starts with b, fails at the same block, and ends before b again,
# and only then do the tries start: the log has two lines.
#

verify_runnable "global"

typeset event_file=$TEST_BASE_DIR/async_mirror_reread_events.$$

function cleanup
{
	[[ -e $ASYNC_MIRROR_HIDDEN ]] && async_mirror_unhide_leader
	cleanup_async_mirror
	rm -f $event_file
}

#
# Print the digests of the files of the test.
#
function all_digests
{
	async_mirror_digests a 2
	async_mirror_digests p 3
	xxh128digest /$TESTPOOL/b
	async_mirror_digests q 3
}

#
# Print the number of times that the debug log says that a commit ended
# before the txg of file b because of a block that cannot be read.
#
# $1 the txg of file b
#
function cut_count # txg
{
	async_mirror_dbgmsg_count "async mirror: txg $1: a block cannot be" \
	    "the commit ends before this txg"
}

log_onexit cleanup

log_assert "A txg with a read error in a commit is copied alone, not sorted" \
    "again."

log_must async_mirror_set_tunable32 DBGMSG_ENABLE 1
async_mirror_create_pool
async_mirror_enable
async_mirror_write_files a 2
log_must wait_async_mirror_idle

# 8 tries, 3 s apart: time to look at the copy while b waits for them.
log_must async_mirror_set_tunable32 ASYNC_MIRROR_READ_RETRIES 8
async_mirror_suspend_copy 1
async_mirror_write_files p 3
async_mirror_write /$TESTPOOL/b 4
sync_pool $TESTPOOL
async_mirror_write_files q 3
# The txgs that wrote the data, not the synced txg after each sync.
typeset -i txg_p=$(async_mirror_file_birth p.2)
typeset -i txg_b=$(async_mirror_file_birth b)
log_must test $txg_p -gt 0
typeset digests="$(all_digests)"
log_must zpool export $TESTPOOL
async_mirror_import
typeset -i pending=$(get_async_mirror_stat pending_txgs)
log_must test $pending -ge 7
log_must test $txg_b -gt $txg_p
log_must zpool events -c
log_must zinject -a -t data -e io -T read /$TESTPOOL/b

typeset -i cuts=$(cut_count $txg_b)
async_mirror_suspend_copy 0
log_must wait_async_mirror_stat read_retries -ge 2
typeset -i applied=$(get_async_mirror_stat applied_txg)
log_note "p ends in txg $txg_p, b is in txg $txg_b, applied txg $applied"
log_must test $applied -ge $txg_p
log_must test $applied -lt $txg_b
log_must test "$(get_async_mirror_stat data_deferred)" == 0
log_must test "$(get_async_mirror_stat frozen)" == 0
typeset -i found=$(( $(cut_count $txg_b) - cuts ))
log_note "$found lines for the commit that ended before txg $txg_b"
log_must test $found -eq 1

log_must zinject -c all
log_must wait_async_mirror_idle
log_must test "$(get_async_mirror_stat data_deferred)" == 0
log_must test "$(get_async_mirror_stat data_errors)" == 0
log_must test "$(get_async_mirror_stat failed)" == 0
log_must test "$(get_async_mirror_stat frozen)" == 0
log_must test $(( $(cut_count $txg_b) - cuts )) -eq 1
log_must eval "zpool events -v > $event_file"
log_must test "$(async_mirror_event_count $event_file \
    async_mirror_retry)" -eq 1

log_must zpool export $TESTPOOL
async_mirror_import_lagging
log_must test "$digests" == "$(all_digests)"
log_must zpool scrub -w $TESTPOOL
log_must check_pool_status $TESTPOOL errors "No known data errors"
log_must test "$(async_mirror_cksum_errors $ASYNC_MIRROR_LAGGING)" == 0
log_must zpool export $TESTPOOL
async_mirror_unhide_leader

log_pass "A txg with a read error in a commit is copied alone, not sorted" \
    "again."
