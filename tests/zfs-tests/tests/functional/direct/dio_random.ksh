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
. $STF_SUITE/tests/functional/direct/dio.cfg
. $STF_SUITE/tests/functional/direct/dio.kshlib

#
# DESCRIPTION:
# 	Verify mixed Direct I/O and buffered I/O. A workload of random
#	but correctly aligned direct read/writes is mixed with a
#	concurrent workload of entirely unaligned buffered read/writes.
#
# STRATEGY:
#	1. Create an empty file.
#	2. Start a background fio randomly issuing direct read/writes.
#	3. Start a background fio randomly issuing buffered read/writes.
#

verify_runnable "global"

function cleanup
{
	log_must rm -f "$tmp_file"
}

log_assert "Verify randomly sized mixed Direct I/O and buffered I/O"

log_onexit cleanup

mntpnt=$(get_prop mountpoint $TESTPOOL/$TESTFS)
tmp_file=$mntpnt/file
bs=$((1024 * 1024))
blocks=32
size=$((bs * blocks))
runtime=10
page_size=$(getconf PAGESIZE)

log_must stride_dd -i /dev/zero -o $tmp_file -b $bs -c $blocks

# Direct random read/write page-aligned IO of varying sizes with
# occasional calls to fsync(2), mixed with...
log_must eval "fio --filename=$tmp_file --name=direct-rwrand \
	--rw=randrw --size=$size --offset_align=$(getconf PAGESIZE) \
	--bsrange=$page_size-1m --direct=1 --fsync=32 --numjobs=2 \
	--ioengine=sync --fallocate=none --verify=sha1 \
	--group_reporting --minimal --runtime=$runtime --time_based &"

# Buffered random read/write entirely unaligned IO of varying sizes
# occasional calls to fsync(2).
log_must eval "fio --filename=$tmp_file --name=buffered-write \
	--rw=randrw --size=$size --offset_align=512 --bs_unaligned=1 \
	--bsrange=$page_size-1m --direct=0 --fsync=32 --numjobs=2 \
	--ioengine=sync --fallocate=none --verify=sha1 \
	--group_reporting --minimal --runtime=$runtime --time_based &"

wait

log_pass "Verfied randomly sized mixed Direct I/O and buffered I/O"
