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
#	A read racing truncates returns the old data or zeros, and
#	stops only at a size the file had.
#
# STRATEGY:
#	1. In a file system with recordsize=128k, write a file of
#	   7 * 128K + 2560 bytes.
#	2. One thread loops.  It truncates the file to
#	   3 * 128K + 5096 bytes, extends it back, which leaves
#	   zeros past the cut, and rewrites the tail with a new
#	   generation.  Four threads read ranges that cross the
#	   cut.
#	3. Each read must return the unchanged data below the cut.
#	   Above it a read holds nothing, zeros or one rewrite, and
#	   ends at one of the two sizes or where it was asked to.
#	   The state it saw must have been current while it ran.
#	4. Fail on any other read, or if no read overlapped a
#	   truncate.
#

verify_runnable "global"

FS=$TESTPOOL/$TESTFS/truncate

function cleanup
{
	datasetexists $FS && destroy_dataset $FS -r
}

log_assert "Reads racing truncates return old data or zeros"
log_onexit cleanup

log_must zfs create -o recordsize=128k $FS
rl_run 60 120 truncate $(get_prop mountpoint $FS)/file

log_pass "Reads racing truncates return old data or zeros"
