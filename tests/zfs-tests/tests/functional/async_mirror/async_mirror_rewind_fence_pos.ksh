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
#	A read-only rewind import to a txg between the lagging child and
#	the leader does not read blocks from the lagging child that are
#	newer than its txg, and does not change it.  A rewind with -T
#	verifies every block of the pool, from both children.
#
# STRATEGY:
#	1. Create a pool with a two-way mirror and turn the async mirror on.
#	2. Write files, and wait until the lagging child has caught up.
#	3. Stop the copy.  Write four files, and keep the txg of the pool.
#	   Write more files, and export the pool.
#	4. Import the pool read-only at the kept txg, with -T.
#	5. Verify the data of that txg, that the lagging child has no
#	   checksum errors, and that it is unchanged.
#
# The import verifies with reads that do not count checksum errors, and a
# read-only import does not repair.  So without the fence, only the reads of
# the files show the problem, when the mirror picks the lagging child for
# them.  The test reads four files to make that likely.  A lagging child
# that is ahead of the rewind txg is a different case.
#

verify_runnable "global"

log_onexit cleanup_async_mirror

async_mirror_create_pool
async_mirror_enable

async_mirror_write_files old 2
log_must wait_async_mirror_idle
typeset digests="$(async_mirror_digests old 2)"

async_mirror_suspend_copy 1
async_mirror_write_files mid 4
digests="$digests
$(async_mirror_digests mid 4)"
typeset -i txg=$(get_async_mirror_pool_txg)
async_mirror_write_files new 4
log_must zpool export $TESTPOOL

typeset before=$(xxh128digest $ASYNC_MIRROR_LAGGING)

async_mirror_import_txg $txg -o readonly=on
log_must test "$digests" == "$(async_mirror_digests old 2)
$(async_mirror_digests mid 4)"
log_must test "$(async_mirror_cksum_errors $ASYNC_MIRROR_LAGGING)" -eq 0
log_must zpool export $TESTPOOL
log_must test "$before" == "$(xxh128digest $ASYNC_MIRROR_LAGGING)"

log_pass "A rewind import does not read newer blocks from the lagging child."
