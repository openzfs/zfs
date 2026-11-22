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
#	The async mirror turns on for a mirror that already holds data.  The
#	lagging child then holds every block born up to its txg.  When the
#	leader cannot supply such a block, the pool must read it from the
#	lagging child and repair the leader.  It must not fence off the only
#	good copy: for metadata of the pool, that suspends the pool in the
#	first sync of the import.
#
# STRATEGY:
#	1. Create a pool with a two-way mirror, with the async mirror off.  Write
#	   files a, then files b, each in a txg of its own, so that the
#	   import does not verify the blocks of files a.
#	2. Damage every copy of the indirect block and of the data blocks
#	   of file a.0 on the leader only, and export the pool.
#	3. Turn the async mirror on, and export and import the pool, so that the
#	   reads miss the ARC and the import runs with the async mirror on.
#	   Verify that the async mirror is on, that files a are intact, and that
#	   the leader, not the lagging child, counted checksum errors.
#	4. Write files c, export and import the pool.  Verify that the async mirror
#	   is on, that files a, b and c are intact, and that the pool has no
#	   data errors.
#	5. Export the pool and import the leader alone, read-only.  Verify
#	   files a: the reads repaired the leader.
#

verify_runnable "global"

log_onexit cleanup_async_mirror

async_mirror_create_pool
async_mirror_write_files a 2
async_mirror_write_files b 4
typeset digests_a="$(async_mirror_digests a 2)"
typeset digests_b="$(async_mirror_digests b 4)"

async_mirror_corrupt_leader /$TESTPOOL/a.0 0
async_mirror_corrupt_leader /$TESTPOOL/a.0 1
async_mirror_enable
log_must zpool export $TESTPOOL
async_mirror_import
log_must test "$(get_async_mirror_stat active)" == 1
log_must test "$digests_a" == "$(async_mirror_digests a 2)"
log_must test "$(async_mirror_cksum_errors $ASYNC_MIRROR_LEADER)" -gt 0
log_must test "$(async_mirror_cksum_errors $ASYNC_MIRROR_LAGGING)" -eq 0

async_mirror_write_files c 2
typeset digests_c="$(async_mirror_digests c 2)"
log_must zpool export $TESTPOOL
async_mirror_import
log_must test "$(get_async_mirror_stat active)" == 1
log_must test "$digests_a" == "$(async_mirror_digests a 2)"
log_must test "$digests_b" == "$(async_mirror_digests b 4)"
log_must test "$digests_c" == "$(async_mirror_digests c 2)"
log_must check_pool_status $TESTPOOL errors "No known data errors"

log_must zpool export $TESTPOOL
log_must mv $ASYNC_MIRROR_LAGGING $ASYNC_MIRROR_LAGGING_HIDDEN
log_must zpool import -o readonly=on -d $ASYNC_MIRROR_DIR $TESTPOOL
log_must test "$digests_a" == "$(async_mirror_digests a 2)"
log_must zpool export $TESTPOOL
async_mirror_unhide_lagging

log_pass "The lagging child supplies an older block that the leader lacks."
