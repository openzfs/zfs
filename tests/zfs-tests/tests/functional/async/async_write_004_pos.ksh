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
#	Benchmark sync vs async DIO writes across 8 cells:
#	{sync, async} × {1-job, 64-jobs} × {iodepth=1, iodepth=64}.
#

verify_runnable "global"

typeset -i NJ=64

function cleanup
{
	rm -f "$mntpnt/async"*
}

log_assert "Compare sync vs async DIO writes: threads × iodepth matrix"

log_onexit cleanup

if ! is_linux; then
	log_note "Async DIO write test requires Linux"; log_pass
fi

if ! fio_ioengine_available "libaio"; then
	log_note "fio libaio ioengine not available"; log_pass
fi

mntpnt=$(get_prop mountpoint $TESTPOOL/$TESTFS)
runtime=10

# --- sync ---
if tunable_exists ASYNC_DIO_ENABLED; then
	log_must set_tunable32 ASYNC_DIO_ENABLED 0
fi

iops_s1d1=$(async_dio_iops "$mntpnt" "libaio" "write" 1 "$runtime" "sync-1j-1d")
log_note "Sync  1 job  x iodepth=1  → $iops_s1d1 IOPS"

iops_s1d64=$(async_dio_iops "$mntpnt" "libaio" "write" 64 "$runtime" "sync-1j-64d")
log_note "Sync  1 job  x iodepth=64 → $iops_s1d64 IOPS"

iops_sNj1=$(async_dio_iops "$mntpnt" "libaio" "write" 1 "$runtime" "sync-${NJ}j-1d" $NJ)
log_note "Sync ${NJ} jobs x iodepth=1  → $iops_sNj1 IOPS"

iops_sNj64=$(async_dio_iops "$mntpnt" "libaio" "write" 64 "$runtime" "sync-${NJ}j-64d" $NJ)
log_note "Sync ${NJ} jobs x iodepth=64 → $iops_sNj64 IOPS"

# --- async ---
if tunable_exists ASYNC_DIO_ENABLED; then
	log_must set_tunable32 ASYNC_DIO_ENABLED 1
fi

iops_a1d1=$(async_dio_iops "$mntpnt" "libaio" "write" 1 "$runtime" "async-1j-1d")
log_note "Async 1 job  x iodepth=1  → $iops_a1d1 IOPS"

iops_a1d64=$(async_dio_iops "$mntpnt" "libaio" "write" 64 "$runtime" "async-1j-64d")
log_note "Async 1 job  x iodepth=64 → $iops_a1d64 IOPS"

iops_aNj1=$(async_dio_iops "$mntpnt" "libaio" "write" 1 "$runtime" "async-${NJ}j-1d" $NJ)
log_note "Async ${NJ} jobs x iodepth=1  → $iops_aNj1 IOPS"

iops_aNj64=$(async_dio_iops "$mntpnt" "libaio" "write" 64 "$runtime" "async-${NJ}j-64d" $NJ)
log_note "Async ${NJ} jobs x iodepth=64 → $iops_aNj64 IOPS"

async_dio_verify "$mntpnt" "libaio" 64

log_note "============================================"
log_note " Results matrix (libaio randwrite, 128K):"
log_note ""
log_note "  Configuration                    IOPS"
log_note "  ────────────────────────────────  ──────"
log_note "  Sync   1 job  × iodepth=1       $iops_s1d1"
log_note "  Sync   1 job  × iodepth=64      $iops_s1d64"
log_note "  Async  1 job  × iodepth=1       $iops_a1d1"
log_note "  Async  1 job  × iodepth=64      $iops_a1d64"
log_note "  Sync   ${NJ} jobs × iodepth=1       $iops_sNj1"
log_note "  Async  ${NJ} jobs × iodepth=1       $iops_aNj1"
log_note "  Sync   ${NJ} jobs × iodepth=64      $iops_sNj64"
log_note "  Async  ${NJ} jobs × iodepth=64      $iops_aNj64"
log_note "============================================"

log_pass "Async vs sync DIO writes: threads × iodepth matrix complete"
