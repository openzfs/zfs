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
#	An import allocates the list of the records of the async mirror
#	with a limit, zfs_vdev_async_mirror_load_max_bytes, so that a
#	damaged pool cannot ask for memory without end.  A pool with more
#	records than the limit allows fails the import, and imports after
#	the limit is raised: the limit never locks a pool out.
#
# STRATEGY:
#	1. Create a pool with a two-way mirror and turn the async mirror on.
#	   Stop the release, write files, and export the pool: the records
#	   stay.
#	2. Set the limit to 64 bytes, less than one record.  Verify that
#	   the import fails.
#	3. Set the limit to 0, which is no limit.  Verify that the pool
#	   imports, that the async mirror is on, and the files.
#

verify_runnable "global"

log_onexit cleanup_async_mirror

async_mirror_create_pool
async_mirror_enable
async_mirror_suspend_release 1
async_mirror_write_files a 3
log_must wait_async_mirror_idle
typeset digests="$(async_mirror_digests a 3)"
log_must zpool export $TESTPOOL

log_must async_mirror_set_tunable64 ASYNC_MIRROR_LOAD_MAX_BYTES 64
log_mustnot zpool import -d $ASYNC_MIRROR_DIR $TESTPOOL
log_mustnot poolexists $TESTPOOL

log_must async_mirror_set_tunable64 ASYNC_MIRROR_LOAD_MAX_BYTES 0
async_mirror_import
log_must test "$(get_async_mirror_stat active)" -eq 1
log_must test "$digests" == "$(async_mirror_digests a 3)"
async_mirror_suspend_release 0
log_must verify_pool $TESTPOOL

log_pass "The limit of the record list fails an import, and a raised" \
    "limit lets the pool in."
