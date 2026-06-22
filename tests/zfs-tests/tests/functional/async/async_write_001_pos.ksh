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
#	Verify async Direct I/O writes with libaio — data integrity and
#	IOPS at multiple iodepths using the async write path.
#
# STRATEGY:
#	1. Ensure zfs_async_dio_enabled is enabled
#	2. Benchmark write IOPS with libaio at iodepth=1 and iodepth=64
#	3. Verify data integrity with sha1 at iodepth=64
#	4. Log IOPS for comparison
#

verify_runnable "global"

function cleanup
{
	rm -f "$mntpnt/async"*
}

log_assert "Verify async DIO writes with libaio: data integrity and IOPS"

log_onexit cleanup

if ! is_linux; then
	log_note "Async DIO write test requires Linux"; log_pass
fi

if ! fio_ioengine_available "libaio"; then
	log_note "fio libaio ioengine not available"; log_pass
fi

mntpnt=$(get_prop mountpoint $TESTPOOL/$TESTFS)
runtime=10

# Ensure async DIO is enabled (setup already enables it)
if tunable_exists ASYNC_DIO_ENABLED; then
	log_must set_tunable32 ASYNC_DIO_ENABLED 1
fi

log_note "--- Async write benchmark (zfs_async_dio_enabled=1) ---"

iops_d1=$(async_dio_write_iops "$mntpnt" "libaio" 1 "$runtime" "async-write-d1")
log_note "Async IOPS (iodepth=1): $iops_d1"

iops_d64=$(async_dio_write_iops "$mntpnt" "libaio" 64 "$runtime" "async-write-d64")
log_note "Async IOPS (iodepth=64): $iops_d64"

# --- Data integrity verification ---
log_note "--- Data integrity verification ---"
async_dio_write_verify "$mntpnt" "libaio" 64

# --- Summary ---
log_note "============================================"
log_note "IOPS Summary (libaio randwrite, 128K blocks):"
log_note "  iodepth=1:  $iops_d1"
log_note "  iodepth=64: $iops_d64"
log_note "============================================"

log_pass "Async DIO writes with libaio passed"
