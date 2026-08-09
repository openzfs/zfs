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
# Copyright (c) 2026, Michael Heller.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#	A mmap read that faults a page in after the file has been truncated
#	below it must not corrupt memory or panic.
#
# STRATEGY:
#	Several reader processes repeatedly mmap() a file and fault every page
#	while a truncator process churns the file size between 0 and 64M. A
#	read that lands beyond EOF legitimately raises SIGBUS and is tolerated;
#	a read that races the truncate reaches zfs_fillpage() with
#	io_off >= i_size. A correct module zero-fills that page and the run
#	completes; before the fix the unsigned io_len underflowed and dmu_read()
#	zero-filled far past the page (a debug build asserts, a production build
#	silently corrupts memory).
#

verify_runnable "global"

typeset TESTFILE=$TESTDIR/mmap_read_truncate.$$
typeset -i FILESIZE=$((64 * 1024 * 1024))
typeset -i DURATION=15
typeset -i READERS=8

function cleanup
{
	rm -f $TESTFILE
}

log_assert "mmap read racing ftruncate (past EOF) does not corrupt or panic"
log_onexit cleanup

log_must mmap_read_truncate $TESTFILE $FILESIZE $DURATION $READERS

log_pass "mmap read racing ftruncate completed cleanly"
