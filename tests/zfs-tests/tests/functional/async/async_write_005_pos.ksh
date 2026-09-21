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
#	Verify async DIO requests larger than SPA_MAXBLOCKSIZE (16MiB) are
#	handled safely.  The async path caps requests at SPA_MAXBLOCKSIZE and
#	falls back to the synchronous path for larger I/O; this test would
#	previously panic the kernel (VERIFY in abd_alloc_linear) and must
#	now complete with correct data.
#
# STRATEGY:
#	1. Enable zfs_async_dio_enabled
#	2. Write and read back a file with libaio using 32MiB blocks
#	   (each request exceeds SPA_MAXBLOCKSIZE) and verify with sha1
#	3. Write and read back a file with libaio using 16MiB blocks
#	   (exactly SPA_MAXBLOCKSIZE, still served by the async path)
#	   and verify with sha1
#

verify_runnable "global"

function cleanup
{
	rm -f "$mntpnt/async"*
}

log_assert "Verify oversized async DIO requests fall back safely"

log_onexit cleanup

if ! is_linux; then
	log_note "Async DIO test requires Linux"; log_pass
fi

if ! fio_ioengine_available "libaio"; then
	log_note "fio libaio ioengine not available"; log_pass
fi

mntpnt=$(get_prop mountpoint $TESTPOOL/$TESTFS)

# Enable async DIO
if tunable_exists ASYNC_DIO_ENABLED; then
	log_must set_tunable32 ASYNC_DIO_ENABLED 1
fi

#
# 32MiB requests are larger than SPA_MAXBLOCKSIZE: the async path must
# return EOPNOTSUPP so zpl_iter_write/read falls back to the synchronous
# path, which chunks the request.  Before the cap was added this write
# panicked the kernel in abd_alloc_linear().
#
async_dio_verify "$mntpnt" "libaio" 1 32M 128M

#
# 16MiB requests are exactly SPA_MAXBLOCKSIZE: they must still be served
# by the async path (the cap is strictly greater-than).
#
async_dio_verify "$mntpnt" "libaio" 1 16M 64M

log_pass "Oversized async DIO requests handled safely"
