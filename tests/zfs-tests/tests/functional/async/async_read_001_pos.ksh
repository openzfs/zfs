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
#	Verify async Direct I/O reads with libaio — data integrity and
#	IOPS comparison with async disabled vs enabled.
#
# STRATEGY:
#	1. Disable zfs_async_dio_enabled, benchmark IOPS (sync baseline)
#	2. Enable zfs_async_dio_enabled, benchmark IOPS (async path)
#	3. Verify data integrity with sha1
#	4. Log IOPS before/after for comparison
#

verify_runnable "global"

function cleanup
{
	if tunable_exists ASYNC_DIO_ENABLED; then
		log_must restore_tunable ASYNC_DIO_ENABLED
	fi
	rm -f "$mntpnt/async"*
}

log_assert "Verify async DIO reads with libaio: data integrity and IOPS"

log_onexit cleanup

if ! is_linux; then
	log_unsupported "Async DIO read test requires Linux"
fi

if ! fio_ioengine_available "libaio"; then
	log_unsupported "fio libaio ioengine not available"
fi

mntpnt=$(get_prop mountpoint $TESTPOOL/$TESTFS)
runtime=10

# --- Phase 1: Sync baseline (async disabled) ---
if tunable_exists ASYNC_DIO_ENABLED; then
	log_must set_tunable32 ASYNC_DIO_ENABLED 0
fi

log_note "--- Sync baseline (zfs_async_dio_enabled=0) ---"
iops_sync=$(async_dio_read_iops "$mntpnt" "libaio" 1 "$runtime" "sync-read")
log_note "Sync baseline IOPS (iodepth=1): $iops_sync"

iops_sync_d64=$(async_dio_read_iops "$mntpnt" "libaio" 64 "$runtime" "sync-read-d64")
log_note "Sync baseline IOPS (iodepth=64): $iops_sync_d64"

# --- Phase 2: Async path (async enabled) ---
if tunable_exists ASYNC_DIO_ENABLED; then
	log_must set_tunable32 ASYNC_DIO_ENABLED 1
	log_note "--- Async enabled (zfs_async_dio_enabled=1) ---"
else
	log_note "--- Async tunable not available, using sync path ---"
fi

iops_async=$(async_dio_read_iops "$mntpnt" "libaio" 1 "$runtime" "async-read")
log_note "Async IOPS (iodepth=1): $iops_async"

iops_async_d64=$(async_dio_read_iops "$mntpnt" "libaio" 64 "$runtime" "async-read-d64")
log_note "Async IOPS (iodepth=64): $iops_async_d64"

# --- Phase 3: Data integrity verification ---
log_note "--- Data integrity verification ---"
async_dio_read_verify "$mntpnt" "libaio" 64

# --- Summary ---
log_note "============================================"
log_note "IOPS Summary (libaio randread, 128K blocks):"
log_note "  iodepth=1  sync:  $iops_sync"
log_note "  iodepth=1  async: $iops_async"
log_note "  iodepth=64 sync:  $iops_sync_d64"
log_note "  iodepth=64 async: $iops_async_d64"
log_note "============================================"

log_pass "Async DIO reads with libaio passed"
