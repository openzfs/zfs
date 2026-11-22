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
#	A read-only import returns the right data from a dataset with
#	checksum=off while the lagging child is behind.  Such a block has no
#	checksum, so a read of the old data on the lagging child would pass.
#
# STRATEGY:
#	1. Create a pool with a two-way mirror, and a dataset with
#	   checksum=off.  Turn the async mirror on.
#	2. Stop the copy, write a file of 32 MiB to the dataset, and keep
#	   its digest.  Export the pool.
#	3. Import the pool read-only.
#	4. Verify the digest, and that the lagging child has no checksum
#	   errors.
#

verify_runnable "global"

log_onexit cleanup_async_mirror

async_mirror_create_pool
log_must zfs create -o checksum=off $TESTPOOL/nock
async_mirror_enable

async_mirror_suspend_copy 1
log_must file_write -o create -f /$TESTPOOL/nock/data -b 131072 -c 256 -d R
sync_pool $TESTPOOL
typeset digest=$(xxh128digest /$TESTPOOL/nock/data)
log_must test "$(get_async_mirror_stat pending_txgs)" -gt 0
log_must zpool export $TESTPOOL

log_must zpool import -o readonly=on -d $ASYNC_MIRROR_DIR $TESTPOOL
log_must test "$digest" == "$(xxh128digest /$TESTPOOL/nock/data)"
log_must test "$(async_mirror_cksum_errors $ASYNC_MIRROR_LAGGING)" -eq 0
log_must zpool export $TESTPOOL

log_pass "A read-only import returns the right data."
