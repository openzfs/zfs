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
#	A mirror that never used the async mirror has no mark in its config, and
#	works as before.
#
# STRATEGY:
#	1. Create a pool with a two-way mirror.  Do not turn the async mirror on.
#	2. Write files, export the pool and import it.
#	3. Verify that no label has the mark, that the files are right,
#	   that a scrub finds no errors, and that no child has checksum
#	   errors.
#

verify_runnable "global"

log_onexit cleanup_async_mirror

async_mirror_create_pool
async_mirror_write_files file 4
typeset digests="$(async_mirror_digests file 4)"
log_must zpool export $TESTPOOL
async_mirror_import

log_mustnot eval "zdb -l $ASYNC_MIRROR_LEADER | grep -q async_mirror_lagging"
log_mustnot eval "zdb -l $ASYNC_MIRROR_LAGGING | grep -q async_mirror_lagging"
log_must test "$digests" == "$(async_mirror_digests file 4)"
log_must verify_pool $TESTPOOL
log_must check_pool_status $TESTPOOL errors "No known data errors"
log_must test "$(async_mirror_cksum_errors $ASYNC_MIRROR_LEADER)" -eq 0
log_must test "$(async_mirror_cksum_errors $ASYNC_MIRROR_LAGGING)" -eq 0

log_pass "A mirror without the async mirror has no mark."
