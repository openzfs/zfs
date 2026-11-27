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
#	A lagging child that is ahead of a rewind, and that the rewind could
#	not erase, does not make later imports fail as "written
#	separately".  The rewind import, with the child missing, forks the
#	lineage and marks the child for a full resilver.  The child is still
#	on the parent lineage, above the fork: history that the rewind
#	discarded.  The next import with both children accepts it and
#	resilvers it.
#
# STRATEGY:
#	1. Create a pool with a two-way mirror and turn the async mirror on.  Write
#	   files a, wait until the lagging child has caught up, and note the
#	   txg.  Write files b and wait again.  Export.
#	2. Hide the lagging child, and import the leader with a rewind to
#	   the noted txg.  Export, and put the lagging child back.
#	3. Import both children.  Verify that the import works, wait for the
#	   resilver, and verify files a, that b does not exist, and that a
#	   scrub finds no error.
#

verify_runnable "global"

function cleanup
{
	[[ -e $ASYNC_MIRROR_LAGGING_HIDDEN ]] && async_mirror_unhide_lagging
	cleanup_async_mirror
}

log_onexit cleanup

async_mirror_create_pool
async_mirror_enable
async_mirror_write_files a 4
async_mirror_suspend_release 1
#
# The rewind goes back about 30 txgs, over the creation of files b, and
# its import verifies the metadata of the kept txg.  The creation of b.0
# frees the dataset's dnode block of the kept txg, and the pool often
# reused that space before the export: in one run of four the import then
# went back to an older txg, without some or all of files a, or before
# the async mirror was on.  A snapshot keeps the dataset's blocks of files a
# allocated.  That does not keep the MOS blocks and the log space maps of
# the kept txg (one run in five still failed), so the release of records
# also stops from the noted txg until the import: every free is held.
#
log_must zfs snapshot $TESTPOOL@a
log_must wait_async_mirror_idle
typeset digests="$(async_mirror_digests a 4)"
typeset -i txg=$(get_async_mirror_pool_txg)
async_mirror_write_files b 4
log_must wait_async_mirror_idle
log_must zpool export $TESTPOOL

typeset -i newest=$(get_ring_max_txg $ASYNC_MIRROR_LAGGING)
log_note "rewind txg $txg, newest txg of the lagging child $newest"
log_must test $newest -gt $txg

log_must mv $ASYNC_MIRROR_LAGGING $ASYNC_MIRROR_LAGGING_HIDDEN
async_mirror_import_txg $txg
async_mirror_suspend_release 0
log_must test "$(get_async_mirror_lineage lineage_parent)" != 0
log_must zpool export $TESTPOOL
async_mirror_unhide_lagging
log_must test $(get_ring_max_txg $ASYNC_MIRROR_LAGGING) -eq $newest

async_mirror_import
log_must wait_async_mirror_resilvered
log_must test "$digests" == "$(async_mirror_digests a 4)"
log_mustnot test -e /$TESTPOOL/b.0
log_must zpool scrub -w $TESTPOOL
log_must check_pool_status $TESTPOOL errors "No known data errors"
log_must zpool export $TESTPOOL

log_pass "A child left ahead of a rewind is resilvered, not refused."
