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
#	A read error during the copy that passes does not freeze the lagging
#	child.  The copy of the txg is tried again with a delay, and an
#	async_mirror_retry event announces the tries.  When the read works
#	again, the copy goes on.
#
# STRATEGY:
#	1. Create a pool with a two-way mirror and turn the async mirror on.  Write
#	   files a and wait until the lagging child has caught up.
#	2. With the copy stopped, write file b.  Export and import, so that
#	   the ARC does not hold b.  Inject read errors on the indirect block
#	   of b, a metadata block.
#	3. Restart the copy.  Wait until it counts a try, and verify the
#	   event.  Clear the injection.
#	4. Wait until the lagging child has caught up.  Verify that it is
#	   neither frozen nor failed.
#	5. Import the lagging child alone.  Verify files a and b, and that a
#	   scrub repairs nothing.  The error log can still name b, from the
#	   injected errors.
#

verify_runnable "global"

function cleanup
{
	[[ -e $ASYNC_MIRROR_HIDDEN ]] && async_mirror_unhide_leader
	cleanup_async_mirror
}

log_onexit cleanup

async_mirror_create_pool
async_mirror_enable
async_mirror_write_files a 2
log_must wait_async_mirror_idle
log_must async_mirror_set_tunable32 ASYNC_MIRROR_READ_RETRIES 3

async_mirror_suspend_copy 1
async_mirror_write /$TESTPOOL/b 16
sync_pool $TESTPOOL
log_must zpool export $TESTPOOL
async_mirror_import
log_must test "$(get_async_mirror_stat pending_txgs)" -gt 0
log_must zpool events -c
log_must zinject -a -t data -e io -T read -l 1 /$TESTPOOL/b

async_mirror_suspend_copy 0
typeset -i i
log_must wait_async_mirror_stat read_retries -gt 0
log_must eval "zpool events | grep -q async_mirror_retry"
log_must test "$(get_async_mirror_stat frozen)" == 0
log_must zinject -c all

log_must wait_async_mirror_idle
log_must test "$(get_async_mirror_stat frozen)" == 0
log_must test "$(get_async_mirror_stat failed)" == 0
typeset digests="$(async_mirror_digests a 2)
$(xxh128digest /$TESTPOOL/b)"
log_must zpool export $TESTPOOL

async_mirror_import_lagging
log_must test "$digests" == "$(async_mirror_digests a 2)
$(xxh128digest /$TESTPOOL/b)"
log_must zpool scrub -w $TESTPOOL
log_must eval "zpool status $TESTPOOL | grep -q 'repaired 0B'"
log_must zpool export $TESTPOOL
async_mirror_unhide_leader

log_pass "A read error that passes is tried again, with no freeze."
