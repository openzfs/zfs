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
#	A read-write import of the two copies of an async mirror that were
#	written separately is refused, and the refused import writes
#	nothing to either copy.  Before the import finds that the copies
#	were written separately, it reads the MOS of the newest copy.  A
#	mirror read can go to the other copy, where the range of the block
#	holds a block of the other history.  The read fails its checksum
#	and is read from the newest copy.  The import must not repair the
#	other copy with that data: the block that it would overwrite is live
#	in the other history, which the operator can still choose to keep.
#
# STRATEGY:
#	1. Create a pool with a two-way mirror and turn the async mirror on.
#	2. Write files a, and wait until the lagging child has caught up.
#	3. Stop the copy, write many files b, and export the pool.
#	4. Import the lagging child alone, write files c, and export the
#	   pool.  Import the leader alone, change a property, and export
#	   the pool.  The newest txgs of the leader now hold metadata in
#	   ranges where the lagging child holds blocks of files c.
#	5. Bring both copies back.  Many times: verify that an import is
#	   refused, and that neither copy changed.  One refused import reads
#	   the other copy for only some of the blocks, so one try can miss
#	   the repair.
#	6. Verify each copy alone with zdb.
#

verify_runnable "global"

typeset -i TRIES=40

function cleanup
{
	[[ -e $ASYNC_MIRROR_HIDDEN ]] && async_mirror_unhide_leader
	[[ -e $ASYNC_MIRROR_LAGGING_HIDDEN ]] && async_mirror_unhide_lagging
	cleanup_async_mirror
}

log_onexit cleanup

async_mirror_create_pool
async_mirror_enable

async_mirror_write_files a 2
log_must wait_async_mirror_idle
#
# Many txgs, not many bytes: the leader must end above the lagging child.
# 24 files of 2 MiB do not fit the 128 MiB mirror: its slop space is half
# of the pool.
#
async_mirror_suspend_copy 1
async_mirror_write_files b 24 1
log_must zpool export $TESTPOOL
async_mirror_suspend_copy 0

async_mirror_import_lagging
async_mirror_write_files c 2
log_must zpool export $TESTPOOL
async_mirror_unhide_leader

async_mirror_import_leader
log_must zfs set compression=off $TESTPOOL
log_must zpool export $TESTPOOL
async_mirror_unhide_lagging

typeset leader=$(xxh128digest $ASYNC_MIRROR_LEADER)
typeset lagging=$(xxh128digest $ASYNC_MIRROR_LAGGING)
typeset -i i
typeset out
for (( i = 0; i < TRIES; i++ )); do
	out=$(zpool import -d $ASYNC_MIRROR_DIR $TESTPOOL 2>&1) &&
	    log_fail "The import of copies written separately succeeded"
	echo "$out" | grep -q "written separately" ||
	    log_fail "The import failed with another error: $out"
	log_mustnot poolexists $TESTPOOL
	[[ "$(xxh128digest $ASYNC_MIRROR_LAGGING)" == "$lagging" ]] ||
	    log_fail "Refused import $i wrote to the lagging child"
	[[ "$(xxh128digest $ASYNC_MIRROR_LEADER)" == "$leader" ]] ||
	    log_fail "Refused import $i wrote to the leader"
done

log_must mv $ASYNC_MIRROR_LEADER $ASYNC_MIRROR_HIDDEN
log_must zdb -e -p $ASYNC_MIRROR_DIR $TESTPOOL
async_mirror_unhide_leader
log_must mv $ASYNC_MIRROR_LAGGING $ASYNC_MIRROR_LAGGING_HIDDEN
log_must zdb -e -p $ASYNC_MIRROR_DIR $TESTPOOL
async_mirror_unhide_lagging

log_pass "A refused import of copies written separately writes to neither."
