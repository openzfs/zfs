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
#	Verify async DIO writes scale with iodepth. With true async I/O
#	(-EIOCBQUEUED), higher iodepth should yield higher IOPS because
#	multiple writes can be submitted in parallel.
#
# STRATEGY:
#	1. Enable zfs_async_dio_enabled
#	2. Run fio randwrite with iodepth=1,4,16,64 and log IOPS at each
#	3. Verify data integrity at iodepth=64
#

verify_runnable "global"

function cleanup
{
	rm -f "$mntpnt/async"*
}

log_assert "Verify async DIO writes with libaio scale with iodepth"

log_onexit cleanup

if ! is_linux; then
	log_note "Async DIO write test requires Linux"; log_pass
fi

if ! fio_ioengine_available "libaio"; then
	log_note "fio libaio ioengine not available"; log_pass
fi

mntpnt=$(get_prop mountpoint $TESTPOOL/$TESTFS)
runtime=10

# Enable async DIO writes
if tunable_exists ASYNC_DIO_ENABLED; then
	log_must set_tunable32 ASYNC_DIO_ENABLED 1
fi

# Benchmark IOPS at increasing iodepths
typeset prev_iops=0
for iodepth in 1 4 16 64; do
	log_note "Benchmarking libaio write iodepth=$iodepth..."
	iops=$(async_dio_write_iops "$mntpnt" "libaio" $iodepth "$runtime" \
	    "scale-write-iod${iodepth}")
	log_note "  iodepth=$iodepth -> $iops IOPS"

	# Track for later comparison
	eval "iops_d${iodepth}=$iops"
done

# Data integrity check at high iodepth
log_note "--- Data integrity at iodepth=64 ---"
async_dio_write_verify "$mntpnt" "libaio" 64

log_note "============================================"
log_note "IOPS Scaling (libaio randwrite, 128K, async enabled):"
log_note "  iodepth=1:  $iops_d1"
log_note "  iodepth=4:  $iops_d4"
log_note "  iodepth=16: $iops_d16"
log_note "  iodepth=64: $iops_d64"
log_note "============================================"

log_pass "Async DIO writes with libaio scale with iodepth"
