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
# Copyright (c) 2026 by Will Rouesnel. All rights reserved.
#
#
# DESCRIPTION:
#	FS_IOC_FSSETXATTR that pins a file and changes its project ID does
#	both or neither, including when the file's first write races it.
#
# STRATEGY:
#	1. 300 times, create an empty file and set a project ID and an 8K
#	   block size on it, cycling through three cases: no write, which
#	   must succeed; a child's one-byte write first, which must fail with
#	   EINVAL and change nothing; and the write racing the call, which
#	   must do one or the other.
#	2. Check every call's result against what the file then reports.
#

. $STF_SUITE/tests/functional/fileblocksize/fileblocksize.kshlib

verify_runnable "both"

typeset d=$TESTDIR/race

function cleanup
{
	rm -rf $d
}

log_onexit cleanup
log_assert "a write racing FS_IOC_FSSETXATTR doesn't leave half of it done"

log_must mkdir $d
log_must extsize -W 300 -p 1015 -s 8192 $d

log_pass "a write racing FS_IOC_FSSETXATTR doesn't leave half of it done"
