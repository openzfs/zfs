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
#	A read whose copy to user memory faults partway returns the
#	data of one write, never parts of two.
#
# STRATEGY:
#	1. In a file system with recordsize=128k, write 4M of
#	   generation 0.
#	2. One thread rewrites all 4M with one pwrite, a new
#	   generation each time.  Two threads read all 4M into a
#	   new anonymous mapping after writing only its first 64K,
#	   so the copy faults partway through.  Each reader waits
#	   for a gap between writes before it starts a read.
#	3. Each read must hold one generation that was current
#	   while it ran.
#	4. Fail on any other read, or if no write began during a
#	   read that began with none in flight.
#

verify_runnable "global"

FS=$TESTPOOL/$TESTFS/cold

function cleanup
{
	datasetexists $FS && destroy_dataset $FS -r
}

log_assert "Reads that fault partway return one write's data"
log_onexit cleanup

log_must zfs create -o recordsize=128k $FS
rl_run 60 120 -r 2 cold $(get_prop mountpoint $FS)/file

log_pass "Reads that fault partway return one write's data"
