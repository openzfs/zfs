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
#	A read of a range that a write replaces returns the data
#	from before the write or from after it, never a mix.
#
# STRATEGY:
#	1. In a file system with recordsize=128k, fill 8 regions
#	   of 128K.  Each 8-byte word holds a generation and its
#	   position.
#	2. Two threads rewrite whole regions with new generations.
#	   Four threads read whole regions, parts of one, and the
#	   second half of one with the first half of the next.
#	3. Each read must hold one generation per region, and one
#	   that was current while the read ran.  Count the reads
#	   that were in flight while a write of their range was.
#	4. Fail on any other read, or if no read overlapped a
#	   write.
#

verify_runnable "global"

FS=$TESTPOOL/$TESTFS/regions

function cleanup
{
	datasetexists $FS && destroy_dataset $FS -r
}

log_assert "Reads racing writes of the same range are not torn"
log_onexit cleanup

log_must zfs create -o recordsize=128k $FS
MNT=$(get_prop mountpoint $FS)
log_must rangelock_race -p regions $MNT/file
sync_pool $TESTPOOL
rl_run 60 120 regions $MNT/file

log_pass "Reads racing writes of the same range are not torn"
