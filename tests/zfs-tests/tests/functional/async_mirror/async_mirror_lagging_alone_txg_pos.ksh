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
#	When the leader is missing, the lagging child alone serves its own
#	txg, read-only and writable.  A writable import turns the async mirror off
#	and clears the mark in the config.  This passes with and without the
#	fence: it guards the case in which the marked child is the only
#	copy.
#
# STRATEGY:
#	1. Create a pool with a two-way mirror and turn the async mirror on.
#	2. Write files, and wait until the lagging child has caught up.
#	3. Stop the copy, write more files, and export the pool.
#	4. Hide the leader, and import the lagging child alone, read-only.
#	   Verify the files that it has.
#	5. Export the pool, and import it writable.  Verify the files, and
#	   that the labels of the lagging child have no mark.
#

verify_runnable "global"

log_onexit cleanup_async_mirror

async_mirror_create_pool
async_mirror_enable

async_mirror_write_files old 2
log_must wait_async_mirror_idle
typeset digests="$(async_mirror_digests old 2)"
async_mirror_export_behind file 6

log_must mv $ASYNC_MIRROR_LEADER $ASYNC_MIRROR_HIDDEN
log_must zpool import -o readonly=on -d $ASYNC_MIRROR_DIR $TESTPOOL
log_must test "$digests" == "$(async_mirror_digests old 2)"
log_must zpool export $TESTPOOL

async_mirror_import
log_must test "$digests" == "$(async_mirror_digests old 2)"
log_must test "$(get_async_mirror_stat active)" -eq 0
sync_pool $TESTPOOL
log_mustnot eval "zdb -l $ASYNC_MIRROR_LAGGING | grep -q async_mirror_lagging"
log_must zpool export $TESTPOOL

log_pass "The lagging child alone serves its own txg."
