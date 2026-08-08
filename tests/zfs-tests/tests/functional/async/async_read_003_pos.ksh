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
#	Verify that asynchronous Direct I/O reads which cannot be served by
#	the Direct I/O DMA path copy the ARC data into the pages pinned at
#	submission time (the pinned-copy branch of zfs_uiomove_iter()),
#	instead of copying through the user virtual addresses from the taskq
#	thread.
#
# STRATEGY:
#	1. EOF tail: a page-unaligned file is read with a single aligned
#	   async DIO request.  The aligned portion is read with DMA and the
#	   tail through the ARC.  The pinned-copies counter must advance.
#	2. Read-time decline: with direct=disabled, an aligned async DIO
#	   request is declined in zfs_setup_direct() at execution time and
#	   served entirely through the ARC.  The pinned-copies counter must
#	   advance and fio sha1 verification must pass.
#

verify_runnable "global"

mntpnt=$(get_prop mountpoint $TESTPOOL/$TESTFS)
orig_direct=

function cleanup
{
	if [[ -n "$orig_direct" ]]; then
		log_must zfs set direct=$orig_direct $TESTPOOL/$TESTFS
	fi
	[[ -n "$mntpnt" ]] && rm -f "$mntpnt/async_pin"*
}

log_assert "Async DIO reads use pre-pinned pages for the ARC copy fallback"

log_onexit cleanup

if ! is_linux; then
	log_note "Async DIO read test requires Linux"; log_pass
fi

if ! tunable_exists ASYNC_DIO_ENABLED; then
	log_note "zfs_async_dio_enabled tunable not available"; log_pass
fi

if ! tunable_exists ASYNC_READ_PINNED_COPIES 2>/dev/null; then
	log_note "zfs_async_read_pinned_copies tunable not available " \
	    "(debug builds only); skipping"
	log_pass
fi

if ! fio_ioengine_available "libaio"; then
	log_note "fio libaio ioengine not available"; log_pass
fi

orig_direct=$(zfs get -H -o value direct $TESTPOOL/$TESTFS)

#
# 1. EOF tail: 4196-byte file read with a single aligned 8 KiB async DIO
# request.  The first 4096 bytes are read by the Direct I/O DMA path and
# the final 100 bytes through the ARC, copied into the pre-pinned pages.
#
tailfile="$mntpnt/async_pin_tail"
log_must dd if=/dev/urandom of="$tailfile" bs=1 count=4196 status=none
log_must set_tunable32 ASYNC_READ_PINNED_COPIES 0
# With --readonly fio also tries to invalidate the page cache for the range
# beyond EOF, which fails with EBADF because no writable fd is open.  fio
# prints that benign warning to stdout; drop stdout so the test log stays
# clean.  The exit status still reflects the read itself.
log_must eval "fio --filename=\"$tailfile\" --name=\"async-pin-tail\" \
    --rw=read --bs=8K --size=8K --direct=1 --ioengine=libaio \
    --iodepth=1 --readonly >/dev/null"
copies=$(get_tunable ASYNC_READ_PINNED_COPIES)
if (( copies < 1 )); then
	log_fail "pinned-page ARC copy not used for the EOF tail " \
	    "(counter=$copies)"
fi
log_note "EOF tail used pinned-page ARC copy (counter=$copies)"
rm -f "$tailfile"

#
# 2. Read-time decline: with direct=disabled, every O_DIRECT read is
# declined in zfs_setup_direct() at execution time and served entirely
# through the ARC from the taskq thread, using the pages pinned at
# submission.  fio sha1 verification confirms the copied data is correct.
#
log_must zfs set direct=disabled $TESTPOOL/$TESTFS
file="$mntpnt/async_pin_fallback"
log_must async_write_verify "$file" 1 "libaio" 8
log_must set_tunable32 ASYNC_READ_PINNED_COPIES 0
log_must async_read_verify "$file" 1 "libaio" 8
copies=$(get_tunable ASYNC_READ_PINNED_COPIES)
if (( copies < 1 )); then
	log_fail "pinned-page ARC copy not used for the direct=disabled " \
	    "fallback (counter=$copies)"
fi
log_note "direct=disabled fallback used pinned-page ARC copy " \
    "(counter=$copies)"

log_must zfs set direct=$orig_direct $TESTPOOL/$TESTFS
orig_direct=

log_pass "Async DIO reads use pre-pinned pages for the ARC copy fallback"
