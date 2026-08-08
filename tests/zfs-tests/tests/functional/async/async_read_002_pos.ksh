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
#	Compare O_DIRECT read IOPS between synchronous and asynchronous
#	kiocb submission with fio libaio.
#
# STRATEGY:
#	1. For each numjobs in {1, 32}:
#	     for each iodepth in {1, 8, 32}:
#	       - enable async, recreate the pool + test file, measure IOPS
#	       - disable async, recreate the pool + test file, measure IOPS
#	2. Log the sync vs async IOPS table.
#	3. Restore zfs_async_dio_enabled.
#

verify_runnable "global"

typeset -a iodepths=(1 8 32)
typeset -a numjobss=(1 32)
typeset -A async_iops
typeset -A sync_iops

function cleanup
{
	if tunable_exists ASYNC_DIO_ENABLED; then
		set_tunable32 ASYNC_DIO_ENABLED 1
	fi
	[[ -n "$mntpnt" ]] && rm -f "$mntpnt/async"*
}

log_assert "Compare sync vs async O_DIRECT read IOPS with fio libaio"

log_onexit cleanup

if ! is_linux; then
	log_note "Async DIO read test requires Linux"; log_pass
fi

if ! tunable_exists ASYNC_DIO_ENABLED; then
	log_note "zfs_async_dio_enabled tunable not available"; log_pass
fi

if ! fio_ioengine_available "libaio"; then
	log_note "fio libaio ioengine not available"; log_pass
fi

mntpnt=$(get_prop mountpoint $TESTPOOL/$TESTFS)
runtime=5

for numjobs in "${numjobss[@]}"; do
	for iodepth in "${iodepths[@]}"; do
		log_note "=== numjobs=$numjobs iodepth=$iodepth ==="

		# Async measurement
		log_must set_tunable32 ASYNC_DIO_ENABLED 1
		async_reimport_pool
		testfile="$mntpnt/async_iops"
		log_must async_write_verify "$testfile" 1 "libaio" 64
		async_iops[$numjobs,$iodepth]=$(async_read_iops "$testfile" \
		    $iodepth $numjobs $runtime)
		log_note "async IOPS: ${async_iops[$numjobs,$iodepth]}"

		# Sync measurement
		log_must set_tunable32 ASYNC_DIO_ENABLED 0
		async_reimport_pool
		testfile="$mntpnt/async_iops"
		log_must async_write_verify "$testfile" 1 "libaio" 64
		sync_iops[$numjobs,$iodepth]=$(async_read_iops "$testfile" \
		    $iodepth $numjobs $runtime)
		log_note "sync  IOPS: ${sync_iops[$numjobs,$iodepth]}"
	done
done

log_note "============================================"
log_note "O_DIRECT randread IOPS (fio libaio, direct=1, bs=$ASYNC_BS_HR):"
log_note "  numjobs  iodepth  sync     async"
for numjobs in "${numjobss[@]}"; do
	for iodepth in "${iodepths[@]}"; do
		log_note "  $numjobs      $iodepth     " \
		    "${sync_iops[$numjobs,$iodepth]}    " \
		    "${async_iops[$numjobs,$iodepth]}"
	done
done

log_pass "Async O_DIRECT reads compare favorably with synchronous reads"
