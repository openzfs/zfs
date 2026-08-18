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
#	Verify that fsync()/syncfs() cannot be satisfied by a later write
#	completing early while an earlier queued write is still in flight.
#
# STRATEGY:
#	1. Pre-create two DIO-eligible files and reimport the pool so the
#	   data is no longer cached in the ARC.
#	2. Inject a read delay: a reader thread's async Direct I/O read of
#	   file1's first 4K holds the range lock while stalled in the data
#	   pipeline, which blocks W1 (an async write of the same range) inside
#	   zfs_write_impl().
#	3. aio_barrier_race then calls fsync()/syncfs() and submits W2 on an
#	   undelayed range; W2 completes quickly.
#	4. The helper fails if the barrier returns before W1 completes: a
#	   completed-count barrier is satisfied by W2 finishing early even
#	   though W1 is still blocked.
#

verify_runnable "global"

if ! is_linux; then
	log_unsupported "Asynchronous Direct I/O writes are Linux only"
fi

for tunable in ASYNC_DIO_ENABLED DIO_ENABLED; do
	if ! tunable_exists $tunable; then
		log_unsupported "The $tunable tunable is not available"
	fi
done

command -v aio_barrier_race > /dev/null ||
    log_unsupported "This test requires aio_barrier_race."

typeset mntpnt=$(get_prop mountpoint $TESTPOOL/$TESTFS)
typeset file1="$mntpnt/barrier_w1"
typeset file2="$mntpnt/barrier_w2"
typeset -i delay_ms=10000

typeset orig_async=$(get_tunable ASYNC_DIO_ENABLED)
typeset orig_dio=$(get_tunable DIO_ENABLED)
typeset orig_direct=$(get_prop direct $TESTPOOL/$TESTFS)

function cleanup
{
	zinject -c all > /dev/null 2>&1
	set_tunable32 ASYNC_DIO_ENABLED $orig_async
	set_tunable32 DIO_ENABLED $orig_dio
	zfs set direct=$orig_direct $TESTPOOL/$TESTFS
	rm -f "$file1" "$file2"
}

log_assert "fsync()/syncfs() wait for an earlier write even when a later " \
    "write completes first"

log_onexit cleanup

log_must set_tunable32 ASYNC_DIO_ENABLED 1
log_must set_tunable32 DIO_ENABLED 1
log_must zfs set direct=standard $TESTPOOL/$TESTFS

# Pre-create the files so both 4K writes are DIO-eligible overwrites.
log_must fio --filename="$file1" --name=barrier-w1-init --rw=write \
    --bs=8K --size=8K --direct=1 --ioengine=libaio --iodepth=1 \
    --fallocate=none --group_reporting
log_must fio --filename="$file2" --name=barrier-w2-init --rw=write \
    --bs=4K --size=4K --direct=1 --ioengine=libaio --iodepth=1 \
    --fallocate=none --group_reporting
log_must sync

#
# Drop the ARC for the pool so the reader's Direct I/O read of file1 actually
# reaches the data pipeline, where the injected delay holds it.
#
async_reimport_pool

# Delay reads only: the reader's read stalls while W1's and W2's writes do not.
log_must zinject -E $delay_ms -T read -t data "$file1"

log_must aio_barrier_race "$file1" "$file2" fsync
log_must aio_barrier_race "$file1" "$file2" syncfs

log_pass "fsync()/syncfs() wait for an earlier write even when a later " \
    "write completes first"
