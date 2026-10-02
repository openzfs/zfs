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
#	A scrub which suspends at the root of the $ORIGIN snapshot resumes
#	there and still visits the file systems which descend from it.
#
# STRATEGY:
#	1. In a userspace pool, damage a block of the root file system.
#	2. Resume a scrub past the MOS and the dedup table so that $ORIGIN's
#	   root is the first place where it can suspend, and destroy a file
#	   system in the same TXG, whose frees use up its minimum time.
#	3. Require the scrub to report the damaged block, and repeat until
#	   it has suspended at $ORIGIN: the frees usually take several
#	   milliseconds, but can take less than the one it needs.
#

verify_runnable "global"

function cleanup
{
	rm -rf $workdir
}

log_assert "A scrub which suspends at the origin snapshot resumes there"
workdir=$(mktemp -d $TEST_BASE_DIR/zpool_scrub_origin.XXXXXX) ||
    log_fail "cannot create test directory"
log_onexit cleanup

suspended='\$ORIGIN) with min=[0-9]* max=[0-9]*; suspending=1'
typeset -i attempt
for attempt in 1 2 3 4 5; do
	log_must scan_origin_probe $workdir
	grep -q "$suspended" $workdir/dbgmsg && break
done
log_must grep -q "$suspended" $workdir/dbgmsg

log_pass "A scrub which suspended at the origin snapshot resumed there"
