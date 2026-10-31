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

. $STF_SUITE/tests/functional/rangelock/rangelock.kshlib

#
# DESCRIPTION:
#	A read near the end of a file that O_APPEND writers extend
#	returns whole records or stops at the end of the file.
#
# STRATEGY:
#	1. In a file system with recordsize=128k, two threads
#	   append 256K records, two blocks each, to a new file
#	   until it holds 64.  Each record carries its writer and
#	   sequence number.  Four threads read 2 to 6 records from
#	   a record boundary near the end.  Repeat on new files.
#	2. Each read must return whole records, every record
#	   appended before it began, and none that was not begun
#	   by its end.  Count the reads that were in flight while
#	   an append inside their range was.
#	3. Each file must hold every record whole, and each
#	   writer's records in the order it wrote them.
#	4. Fail on any other read, or if no read overlapped an
#	   append.
#

verify_runnable "global"

FS=$TESTPOOL/$TESTFS/append

function cleanup
{
	datasetexists $FS && destroy_dataset $FS -r
}

log_assert "Reads racing appends return whole records"
log_onexit cleanup

log_must zfs create -o recordsize=128k $FS
rl_run 60 120 append $(get_prop mountpoint $FS)

log_pass "Reads racing appends return whole records"
