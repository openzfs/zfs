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
#	A block that cannot be read in the middle of a commit of several
#	transaction groups ends the commit before the transaction group of
#	that block.  The copy sorts and issues the blocks of all the
#	transaction groups of a commit together, so the error is found after
#	the walk of the later ones.  The transaction groups before the error
#	are still committed.  The one with the error is then copied alone,
#	with its tries, and the copy goes on after it.
#
# STRATEGY:
#	1. Create a pool with a two-way mirror and turn the async mirror on.
#	   Write files a and wait until the lagging child has caught up.
#	2. With the copy stopped, write files p, then file b, then files q,
#	   each in a transaction group of its own.  Export and import, so
#	   that the ARC does not hold b.  Inject read errors on the data of b.
#	3. Restart the copy, and wait for the first try after a read error.
#	   Verify that the lagging child has passed some of the waiting
#	   transaction groups, and not all of them, and that no block is
#	   deferred yet: the commit ended before b.
#	4. Wait until the blocks of b are deferred, and clear the injection.
#	   Wait until the lagging child has caught up.  Verify that the
#	   blocks were recovered, that no data error was counted, the
#	   async_mirror_retry event, and that the copy neither failed nor
#	   froze.
#	5. Export the pool and import the lagging child alone.  Verify every
#	   file, and that a scrub finds no error.
#

verify_runnable "global"

typeset event_file=$TEST_BASE_DIR/async_mirror_commit_events.$$

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

log_onexit cleanup

log_assert "A read error in a commit of many txgs ends the commit before it."

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
typeset digests="$(all_digests)"
log_must zpool export $TESTPOOL
async_mirror_import
typeset -i pending=$(get_async_mirror_stat pending_txgs)
log_must test $pending -ge 7
log_must zpool events -c
log_must zinject -a -t data -e io -T read /$TESTPOOL/b

async_mirror_suspend_copy 0
log_must wait_async_mirror_stat read_retries -gt 0
typeset -i left=$(get_async_mirror_stat pending_txgs)
log_note "$pending txgs waited, $left wait during the tries of b"
log_must test $left -gt 0
log_must test $left -lt $pending
log_must test "$(get_async_mirror_stat data_deferred)" == 0
log_must test "$(get_async_mirror_stat frozen)" == 0

log_must wait_async_mirror_stat data_deferred -gt 0
log_must zinject -c all
log_must wait_async_mirror_idle
log_must test "$(get_async_mirror_stat data_recovered)" -ge 1
log_must test "$(get_async_mirror_stat data_errors)" == 0
log_must test "$(get_async_mirror_stat failed)" == 0
log_must test "$(get_async_mirror_stat frozen)" == 0
log_must eval "zpool events -v > $event_file"
log_must grep -q 'sysevent\.fs\.zfs\.async_mirror_retry' $event_file

log_must zpool export $TESTPOOL
async_mirror_import_lagging
log_must test "$digests" == "$(all_digests)"
log_must zpool scrub -w $TESTPOOL
log_must check_pool_status $TESTPOOL errors "No known data errors"
log_must test "$(async_mirror_cksum_errors $ASYNC_MIRROR_LAGGING)" == 0
log_must zpool export $TESTPOOL
async_mirror_unhide_leader

log_pass "A read error in a commit of many txgs ends the commit before it."
