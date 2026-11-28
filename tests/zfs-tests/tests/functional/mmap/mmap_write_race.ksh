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

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#	Data written with write() must read back correctly through a shared
#	mapping of the file while the file is still being written, and
#	page faults racing with the writes must not leave pages in the page
#	cache with zeros in place of data already written (openzfs #14548).
#
# STRATEGY:
#	1. Write small records to a file with write() while another thread
#	   reads each one back through a shared mapping as soon as its write
#	   returns, repeatedly for a while, into a file extended to its full
#	   size up front, into a file that grows with each write, and into
#	   one that grows through a descriptor opened with O_APPEND.
#	2. Verify that every record reads back correctly.
#

verify_runnable "global"

function cleanup
{
	log_must rm -f $TESTDIR/mmap_write_race.*
}

log_assert "Data written with write() reads back correctly through a" \
    "mapping while the file is being written"

log_onexit cleanup

log_must mmap_write_race $TESTDIR/mmap_write_race.prealloc 8 20
log_must mmap_write_race -a $TESTDIR/mmap_write_race.append 8 20
log_must mmap_write_race -A $TESTDIR/mmap_write_race.oappend 8 20

log_pass "Data written with write() reads back correctly through a" \
    "mapping while the file is being written"
