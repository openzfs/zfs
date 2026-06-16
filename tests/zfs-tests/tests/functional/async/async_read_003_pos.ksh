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
#	Verify async DIO reads with io_uring — IOPS scaling and data
#	integrity.  io_uring exercises the same -EIOCBQUEUED path as
#	libaio but through the modern io_uring submission interface.
#
# STRATEGY:
#	1. Enable zfs_async_dio_enabled
#	2. Benchmark IOPS with io_uring at iodepth=1,16,64
#	3. Verify data integrity at iodepth=64
#

verify_runnable "global"

function cleanup
{
	rm -f "$mntpnt/async"*
}

log_assert "Verify async DIO reads with io_uring: IOPS and integrity"

log_onexit cleanup

if ! is_linux; then
	log_unsupported "Async DIO read test requires Linux"
fi

if ! kernel_supports_io_uring; then
	log_unsupported "Kernel does not support io_uring"
fi

if ! fio_ioengine_available "io_uring"; then
	log_unsupported "fio io_uring ioengine not available"
fi

mntpnt=$(get_prop mountpoint $TESTPOOL/$TESTFS)
runtime=10

# Enable async DIO reads
if tunable_exists ASYNC_DIO_ENABLED; then
	log_must set_tunable32 ASYNC_DIO_ENABLED 1
fi

# Benchmark IOPS at increasing iodepths
for iodepth in 1 16 64; do
	log_note "Benchmarking io_uring iodepth=$iodepth..."
	iops=$(async_dio_read_iops "$mntpnt" "io_uring" $iodepth "$runtime" \
	    "uring-iod${iodepth}")
	log_note "  io_uring iodepth=$iodepth -> $iops IOPS"
	eval "iops_d${iodepth}=$iops"
done

# Data integrity check at high iodepth
log_note "--- Data integrity at iodepth=64 ---"
async_dio_read_verify "$mntpnt" "io_uring" 64

log_note "============================================"
log_note "IOPS (io_uring randread, 128K, async enabled):"
log_note "  iodepth=1:  $iops_d1"
log_note "  iodepth=16: $iops_d16"
log_note "  iodepth=64: $iops_d64"
log_note "============================================"

log_pass "Async DIO reads with io_uring passed"
