#!/bin/ksh -p
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
# See the License for the specific language governing permissions
# and limitations under the License.
#
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
# CDDL HEADER END
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
