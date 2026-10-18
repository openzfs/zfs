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
# Copyright (c) 2026 by George Melikov.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#	A write whose source is a mapped file transfers all of its data.
#
# STRATEGY:
#	1. Write from a private mapping of a hole, so that no source page
#	   is resident and every one of them has to be faulted in.
#	2. Do this for a size which fits in the window zfs_write() faults
#	   in before it takes the range lock, and for one which does not.
#	3. Verify the destination has the size that was written.
#

verify_runnable "global"

srcfile=$TESTDIR/mmap_write_source.src
dstfile=$TESTDIR/mmap_write_source.dst

function cleanup
{
	log_must rm -f $srcfile $dstfile
}

log_assert "A write from a mapped source transfers all of its data"
log_onexit cleanup

for size in $((16 * 1024 * 1024)) $((64 * 1024 * 1024)); do
	log_must mmap_write_source $srcfile $dstfile $size
	log_must test $(stat_size $dstfile) -eq $size
done

log_pass "A write from a mapped source transfers all of its data"
