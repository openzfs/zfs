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
# Copyright 2026, tiehexue <tiehexue@hotmail.com>. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/async/async.cfg

#
# DESCRIPTION:
#	Verify that a Direct I/O write which succeeds for some chunks and then
#	hits a quota error is reported as a positive short write with the
#	correct prefix, on both the synchronous and the asynchronous path.
#
# STRATEGY:
#	1. Create a small dataset with recordsize=128K, compression off (the
#	   pattern is compressible and must not fit under the quota), and a
#	   quota.
#	2. On a fresh dataset, write a 1M pattern buffer to a brand new file
#	   through the synchronous path (dio_write_short sync); then on another
#	   fresh dataset through the async path (libaio).
#	3. The first record of a new file is served through the ARC so the file
#	   blocksize can grow (zfs_grow_blocksize), one transaction; it is the
#	   quota's "free hit" and is committed.  The rest of the request is a
#	   second transaction, which fails with EDQUOT.  A regression in
#	   zfs_write_impl() returned that stale error instead of the short
#	   count, hiding how much data was actually written.  dio_write_short
#	   requires 0 < written < requested and verifies that the file contains
#	   exactly the written prefix.
#

verify_runnable "global"

if ! is_linux; then
	log_unsupported "Asynchronous Direct I/O writes are Linux only"
fi

command -v dio_write_short > /dev/null ||
    log_unsupported "This test requires dio_write_short."

typeset ds="$TESTPOOL/async_quota"
typeset orig_async=0

if tunable_exists ASYNC_DIO_ENABLED; then
	orig_async=$(get_tunable ASYNC_DIO_ENABLED)
else
	log_unsupported "The ASYNC_DIO_ENABLED tunable is not available"
fi

function cleanup
{
	set_tunable32 ASYNC_DIO_ENABLED $orig_async
	zfs destroy "$ds" > /dev/null 2>&1
}

log_assert "Writes crossing a quota report a positive short write with a " \
    "correct prefix, sync and async"

log_onexit cleanup

#
# 64K of quota against a 1M write: the first 128K record of the new file is
# committed (the quota "free hit", ARC path for blocksize growth), then the
# remaining transaction fails with EDQUOT, leaving a short write.  The
# pattern buffer is written by dio_write_short and read back for
# verification.
#
function create_quota_ds
{
	log_must zfs create -o compression=off -o recordsize=128K \
	    -o quota=64K "$ds"
}

# Synchronous path: one write(2) with O_DIRECT.
create_quota_ds
typeset qmnt=$(get_prop mountpoint "$ds")
log_must dio_write_short "$qmnt/short_sync" sync 1048576

# Asynchronous path: one libaio O_DIRECT write queued to the worker taskq.
# Each path runs on its own fresh dataset: the quota is deliberately smaller
# than the first transaction, so a second run could not create its file.
log_must zfs destroy "$ds"
create_quota_ds
log_must set_tunable32 ASYNC_DIO_ENABLED 1
log_must dio_write_short "$qmnt/short_async" async 1048576

log_pass "Quota-crossing writes report a positive short write with a " \
    "correct prefix, sync and async"
