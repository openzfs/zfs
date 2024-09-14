#!/bin/ksh -p
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or https://opensource.org/licenses/CDDL-1.0.
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
