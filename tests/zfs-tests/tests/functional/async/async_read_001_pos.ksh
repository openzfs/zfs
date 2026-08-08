#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or https://opensource.org/licenses/CDDL-1.0.
# See the License for the specific language governing permissions
# and limitations under the License.
#
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
# CDDL HEADER END
#

#
# Copyright 2026, tiehexue <tiehexue@hotmail.com>. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/async/async.cfg
. $STF_SUITE/tests/functional/async/async.kshlib

#
# DESCRIPTION:
#	Verify asynchronous Direct I/O reads return correct data.
#
# STRATEGY:
#	1. Write a large file with fio in verify mode (per-block sha1).
#	2. Read it back with libaio O_DIRECT, iodepth=64 (async path).
#	3. Read it back with libaio buffered, iodepth=64 (sync path).
#	4. Repeat 2-3 with io_uring when available.
#	5. Repeat the O_DIRECT/buffered reads on a small ARC-resident file.
#

verify_runnable "global"

function cleanup
{
	[[ -n "$mntpnt" ]] && rm -f "$mntpnt/async"*
}

log_assert "Verify async Direct I/O reads return correct data"

log_onexit cleanup

if ! is_linux; then
	log_note "Async DIO read test requires Linux"; log_pass
fi

if ! tunable_exists ASYNC_DIO_ENABLED; then
	log_note "zfs_async_dio_enabled tunable not available"; log_pass
fi

mntpnt=$(get_prop mountpoint $TESTPOOL/$TESTFS)
big="$mntpnt/async_big"
small="$mntpnt/async_small"

#
# Large file: not ARC resident, exercises the on-disk read path.
#
log_note "Writing $ASYNC_FILESIZE_HR test file (libaio, O_DIRECT)"
log_must async_write_verify "$big" 1 "libaio" 64

log_note "Reading $ASYNC_FILESIZE_HR with libaio O_DIRECT iodepth=64 (async)"
log_must async_read_verify "$big" 1 "libaio" 64

log_note "Reading $ASYNC_FILESIZE_HR with libaio buffered iodepth=64 (sync)"
log_must async_read_verify "$big" 0 "libaio" 64

if fio_ioengine_available "io_uring"; then
	log_note "Reading $ASYNC_FILESIZE_HR with io_uring O_DIRECT iodepth=64 " \
	    "(async)"
	log_must async_read_verify "$big" 1 "io_uring" 64

	log_note "Reading $ASYNC_FILESIZE_HR with io_uring buffered iodepth=64 " \
	    "(sync)"
	log_must async_read_verify "$big" 0 "io_uring" 64
else
	log_note "fio io_uring ioengine not available; skipping io_uring"
fi

#
# Small file: ARC resident, exercises the cached read path.
#
log_note "Writing $ASYNC_SMALL_FILESIZE_HR test file (libaio, O_DIRECT)"
log_must async_write_verify "$small" 1 "libaio" 16 "$ASYNC_SMALL_FILESIZE_HR"

log_note "Reading $ASYNC_SMALL_FILESIZE_HR with libaio O_DIRECT iodepth=16 " \
    "(async)"
log_must async_read_verify "$small" 1 "libaio" 16 "$ASYNC_SMALL_FILESIZE_HR"

log_note "Reading $ASYNC_SMALL_FILESIZE_HR with libaio buffered iodepth=16 " \
    "(sync)"
log_must async_read_verify "$small" 0 "libaio" 16 "$ASYNC_SMALL_FILESIZE_HR"

log_pass "Async Direct I/O reads returned correct data"
