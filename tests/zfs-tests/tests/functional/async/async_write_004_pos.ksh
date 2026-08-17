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

#
# DESCRIPTION:
#	Verify that fsync() and syncfs() wait for queued asynchronous Direct
#	I/O writes before committing, and never hang waiting on them.
#
# STRATEGY:
#	1. Enable async Direct I/O and pin the Direct I/O availability.
#	2. For each barrier in {fsync, syncfs}: submit a vectored async write
#	   and call the barrier immediately, before waiting for the
#	   completion, so the write is still queued or still executing on the
#	   worker taskq.
#	3. The helper requires the barrier to return, the write to report the
#	   full length, and the file to verify byte for byte.  A barrier that
#	   misses queued writes commits the ZIL early, and a broken barrier
#	   counter (e.g. an uninitialized per-znode value) makes fsync() hang,
#	   which the helper's alarm reports as exit code 3.
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

command -v aio_dio_writev > /dev/null ||
    log_unsupported "This test requires aio_dio_writev."

typeset mntpnt=$(get_prop mountpoint $TESTPOOL/$TESTFS)
typeset testfile="$mntpnt/async_fsync"

typeset orig_async=$(get_tunable ASYNC_DIO_ENABLED)
typeset orig_dio=$(get_tunable DIO_ENABLED)
typeset orig_direct=$(get_prop direct $TESTPOOL/$TESTFS)

function cleanup
{
	set_tunable32 ASYNC_DIO_ENABLED $orig_async
	set_tunable32 DIO_ENABLED $orig_dio
	zfs set direct=$orig_direct $TESTPOOL/$TESTFS
	rm -f "$testfile"
}

log_assert "fsync() and syncfs() wait for queued async Direct I/O writes"

log_onexit cleanup

log_must set_tunable32 ASYNC_DIO_ENABLED 1
log_must set_tunable32 DIO_ENABLED 1
log_must zfs set direct=standard $TESTPOOL/$TESTFS

# fsync() while the write is queued or executing: single and vectored shape.
log_must aio_dio_writev "$testfile" 1 8 fsync
log_must aio_dio_writev "$testfile" 4 8 fsync

# syncfs() has the same ordering requirement at filesystem scope.
log_must aio_dio_writev "$testfile" 4 8 syncfs

log_pass "fsync() and syncfs() wait for queued async Direct I/O writes"
