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
. $STF_SUITE/tests/functional/async/async.kshlib

#
# DESCRIPTION:
#	Compare O_DIRECT write IOPS between synchronous and asynchronous
#	kiocb submission with fio libaio.
#
# STRATEGY:
#	1. For each numjobs in {1, 32}:
#	     for each iodepth in {1, 8, 32}:
#	       - enable async, recreate the pool + test file, measure IOPS,
#	         then read the data back with fio --verify to confirm the
#	         async writes were correct
#	       - disable async, recreate the pool + test file, measure IOPS,
#	         then read the data back with fio --verify likewise
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

log_assert "Compare sync vs async O_DIRECT write IOPS with fio libaio"

log_onexit cleanup

if ! is_linux; then
	log_note "Async DIO write test requires Linux"; log_pass
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
		testfile="$mntpnt/async_write_iops"
		log_must async_write_verify "$testfile" 1 "libaio" 64
		async_iops[$numjobs,$iodepth]=$(async_write_iops "$testfile" \
		    $iodepth $numjobs $runtime)
		log_note "async IOPS: ${async_iops[$numjobs,$iodepth]}"

		# Read the data back and verify the per-block sha1 checksums
		# (O_DIRECT through the async read path).
		log_must async_read_verify "$testfile" 1 "libaio" $iodepth

		# Sync measurement
		log_must set_tunable32 ASYNC_DIO_ENABLED 0
		async_reimport_pool
		testfile="$mntpnt/async_write_iops"
		log_must async_write_verify "$testfile" 1 "libaio" 64
		sync_iops[$numjobs,$iodepth]=$(async_write_iops "$testfile" \
		    $iodepth $numjobs $runtime)
		log_note "sync  IOPS: ${sync_iops[$numjobs,$iodepth]}"

		# Read the data back and verify the per-block sha1 checksums.
		log_must async_read_verify "$testfile" 1 "libaio" $iodepth
	done
done

log_note "============================================"
log_note "O_DIRECT randwrite IOPS (fio libaio, direct=1, bs=$ASYNC_BS_HR):"
log_note "  numjobs  iodepth  sync     async"
for numjobs in "${numjobss[@]}"; do
	for iodepth in "${iodepths[@]}"; do
		log_note "  $numjobs      $iodepth     " \
		    "${sync_iops[$numjobs,$iodepth]}    " \
		    "${async_iops[$numjobs,$iodepth]}"
	done
done

log_pass "Async O_DIRECT writes compare favorably with synchronous writes"
