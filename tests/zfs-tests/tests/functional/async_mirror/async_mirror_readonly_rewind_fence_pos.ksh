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
#	A read-only rewind import, to a txg below the newest txg of the
#	lagging child, never reads a block from the lagging child.  That
#	child holds txgs of the history that the rewind discards, and those
#	can have overwritten blocks that are live at the rewind txg.
#
# STRATEGY:
#	1. Create a pool with a two-way mirror and turn the async mirror on.
#	2. Write files a, wait until the lagging child has caught up, and
#	   keep the txg of the pool.
#	3. Write files b, and wait until the lagging child has caught up.
#	4. Damage the first data block of a.0 on the leader only, and
#	   export the pool.
#	5. Import the pool read-only with -T at the kept txg.  Verify that
#	   a read of a.1 works, and that a read of a.0 fails: the lagging
#	   child does not supply the damaged block.
#	6. Control: export, import read-only at the newest txg, and verify
#	   that the lagging child, which is not ahead, supplies the block.
#

verify_runnable "global"

log_onexit cleanup_async_mirror

async_mirror_create_pool
async_mirror_enable

async_mirror_write_files a 2
async_mirror_suspend_release 1
log_must wait_async_mirror_idle
typeset digest="$(xxh128digest /$TESTPOOL/a.1)"
typeset -i txg=$(get_async_mirror_pool_txg)

async_mirror_write_files b 2
log_must wait_async_mirror_idle
async_mirror_corrupt_leader /$TESTPOOL/a.0 0 1
log_must zpool export $TESTPOOL

typeset -i newest=$(get_ring_max_txg $ASYNC_MIRROR_LAGGING)
log_note "rewind txg $txg, newest txg of the lagging child $newest"
log_must test $newest -gt $txg

async_mirror_import_txg $txg -o readonly=on
async_mirror_suspend_release 0
log_must test "$digest" == "$(xxh128digest /$TESTPOOL/a.1)"
log_mustnot eval "cat /$TESTPOOL/a.0 > /dev/null"
log_must zpool export $TESTPOOL

# Control: the same damage, with no rewind.  The lagging child is at the
# newest txg, so it supplies the block.
log_must zpool import -o readonly=on -d $ASYNC_MIRROR_DIR $TESTPOOL
log_must eval "cat /$TESTPOOL/a.0 > /dev/null"

log_pass "A read-only rewind never reads a lagging child that is ahead."
