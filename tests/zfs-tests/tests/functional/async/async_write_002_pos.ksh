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
#	Verify that a vectored asynchronous Direct I/O write is carried out by
#	the Direct I/O path and delivers the whole request, byte for byte.
#
# STRATEGY:
#	1. Run with zfs_dio_strict=1, so a request declined as misaligned
#	   fails with EINVAL rather than being served through the ARC without
#	   the caller being told.  The request is page-aligned in buffer,
#	   offset and length, so it has no legitimate reason to be declined.
#	2. Pin every other input that can legitimately decline Direct I/O:
#	   the dataset's direct property and zfs_dio_enabled.  Without that a
#	   declined request is served correctly through the ARC and the test
#	   passes while exercising nothing.
#	3. Write with 1, 2, 4 and 16 page-sized segments.  Two or more
#	   segments is what makes the kernel describe the request with a
#	   backing iovec array owned by the submitting aio call rather than a
#	   single pointer; 16 also puts it past UIO_FASTIOV.  The taskq
#	   thread that runs the write must never dereference that array after
#	   io_submit() has returned and freed it.
#	4. Repeat each shape, and run each as its own process, because a
#	   request whose eligibility is decided from released memory can come
#	   out either way depending on what has since reused it.
#	5. A single-segment write is the control.
#

verify_runnable "global"

if ! is_linux; then
	log_unsupported "Asynchronous Direct I/O writes are Linux only"
fi

for tunable in ASYNC_DIO_ENABLED DIO_ENABLED DIO_STRICT; do
	if ! tunable_exists $tunable; then
		log_unsupported "The $tunable tunable is not available"
	fi
done

command -v aio_dio_writev > /dev/null ||
    log_unsupported "This test requires aio_dio_writev."

typeset mntpnt=$(get_prop mountpoint $TESTPOOL/$TESTFS)
typeset testfile="$mntpnt/async_writev"

typeset orig_async=$(get_tunable ASYNC_DIO_ENABLED)
typeset orig_dio=$(get_tunable DIO_ENABLED)
typeset orig_strict=$(get_tunable DIO_STRICT)
typeset orig_direct=$(get_prop direct $TESTPOOL/$TESTFS)

function cleanup
{
	set_tunable32 ASYNC_DIO_ENABLED $orig_async
	set_tunable32 DIO_ENABLED $orig_dio
	set_tunable32 DIO_STRICT $orig_strict
	zfs set direct=$orig_direct $TESTPOOL/$TESTFS
	rm -f "$testfile"
}

log_assert "Vectored async Direct I/O writes are served by the Direct I/O path"

log_onexit cleanup

log_must set_tunable32 ASYNC_DIO_ENABLED 1
log_must set_tunable32 DIO_STRICT 1

#
# Direct I/O has to be available for the request to be eligible at all.  A
# request declined because the dataset or the module has it turned off is
# served correctly through the ARC, so leaving either unpinned would let this
# test pass without exercising the path it is meant to cover.
#
log_must set_tunable32 DIO_ENABLED 1
log_must zfs set direct=standard $TESTPOOL/$TESTFS

# The control first: one segment, no backing array in play.
log_must aio_dio_writev "$testfile" 1 16

# Two or more segments carry a backing array that the submitting call owns.
for nseg in 2 4 16; do
	for pass in 1 2 3 4; do
		log_must aio_dio_writev "$testfile" $nseg 16
	done
done

log_pass "Vectored async Direct I/O writes are served by the Direct I/O path"
