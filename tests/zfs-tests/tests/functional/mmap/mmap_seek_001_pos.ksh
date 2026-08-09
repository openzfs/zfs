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
# Copyright (c) 2021 by Lawrence Livermore National Security, LLC.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/mmap/mmap.cfg

#
# DESCRIPTION:
# lseek() data/holes for an mmap()'d file.
#
# STRATEGY:
# 1. Enable compression and hole reporting for dirty files.
# 2. Call mmap_seek binary test case for various record sizes.
#

verify_runnable "global"

function cleanup
{
	log_must zfs set compression=off $TESTPOOL/$TESTFS
	log_must zfs set recordsize=128k $TESTPOOL/$TESTFS
	log_must rm -f $TESTDIR/test-mmap-file
	log_must set_tunable64 DMU_OFFSET_NEXT_SYNC $dmu_offset_next_sync
}

log_assert "lseek() data/holes for an mmap()'d file."

log_onexit cleanup

# Enable hole reporting for dirty files.
typeset dmu_offset_next_sync=$(get_tunable DMU_OFFSET_NEXT_SYNC)
log_must set_tunable64 DMU_OFFSET_NEXT_SYNC 1

# Compression must be enabled to convert zero'd blocks to holes.
# This behavior is checked by the mmap_seek test.
log_must zfs set compression=on $TESTPOOL/$TESTFS

for bs in 4096 8192 16384 32768 65536 131072; do
	log_must zfs set recordsize=$bs $TESTPOOL/$TESTFS
	log_must mmap_seek $TESTDIR/test-mmap-file $((1024*1024)) $bs
	log_must rm $TESTDIR/test-mmap-file
done

log_pass "lseek() data/holes for an mmap()'d file succeeded."
