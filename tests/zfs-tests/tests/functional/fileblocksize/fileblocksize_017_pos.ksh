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
#	A file that had data and was truncated to empty can be pinned, even
#	while the txg that freed its blocks hasn't synced.
#
# STRATEGY:
#	1. Write several records to a file, truncate it to empty and pin it
#	   at once.
#	2. Check the pin and that new data uses the pinned block size.
#

. $STF_SUITE/tests/functional/fileblocksize/fileblocksize.kshlib

verify_runnable "both"

typeset f=$TESTDIR/truncated

function cleanup
{
	rm -f $f
}

log_onexit cleanup
log_assert "a file truncated to empty can be pinned"

log_must dd if=/dev/urandom of=$f bs=128k count=4 2>/dev/null
log_must truncate -s 0 $f
log_must extsize -s 16384 $f
verify_ext $f extsize 16384
write_mib $f
verify_blksz $f 16384

log_pass "a file truncated to empty can be pinned"
