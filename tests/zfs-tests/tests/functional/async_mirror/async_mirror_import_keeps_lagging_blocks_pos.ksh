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
#	A writable import does not change the blocks or the uberblocks of a
#	lagging child that is behind.  The import verifies the newest blocks,
#	and reads them from both children.  Those blocks are newer than the
#	lagging child, so they must not be read from it, and must not be
#	repaired on it.  The import only rewrites the config in the labels
#	of the child at a txg that it holds, so the labels do not make the
#	child unimportable.
#
# STRATEGY:
#	1. Create a pool with a two-way mirror and turn the async mirror on.
#	2. Write files, and wait until the lagging child has caught up.
#	3. Stop the copy, write more files, and export the pool.
#	4. Import the pool, and verify that the lagging child has no
#	   checksum errors.
#	5. Export the pool.  Verify that the lagging child is unchanged
#	   apart from the config in its labels, that its newest uberblock
#	   is the same, and that the config txg is not above it.
#	6. Restart the copy, import the pool, and wait until the lagging
#	   child has caught up.
#	7. Verify the data, and that a scrub finds no errors.
#

verify_runnable "global"

log_onexit cleanup_async_mirror

async_mirror_create_pool
async_mirror_enable

async_mirror_write_files old 2
log_must wait_async_mirror_idle
async_mirror_export_behind file 6

typeset before=$(digest_except_label_config $ASYNC_MIRROR_LAGGING)
typeset -i ring=$(get_ring_max_txg $ASYNC_MIRROR_LAGGING)

async_mirror_import
typeset digests="$(async_mirror_digests old 2)
$(async_mirror_digests file 6)"
log_must test "$(get_async_mirror_stat active)" -eq 1
log_must test "$(async_mirror_cksum_errors $ASYNC_MIRROR_LAGGING)" -eq 0
log_must zpool export $TESTPOOL
log_must test "$before" == \
    "$(digest_except_label_config $ASYNC_MIRROR_LAGGING)"
log_must test $ring -eq $(get_ring_max_txg $ASYNC_MIRROR_LAGGING)
log_must label_txg_held $ASYNC_MIRROR_LAGGING

async_mirror_suspend_copy 0
async_mirror_import
log_must wait_async_mirror_idle
log_must test "$digests" == "$(async_mirror_digests old 2)
$(async_mirror_digests file 6)"
log_must verify_pool $TESTPOOL
log_must check_pool_status $TESTPOOL errors "No known data errors"

log_pass "A writable import does not change a lagging child that is behind."
