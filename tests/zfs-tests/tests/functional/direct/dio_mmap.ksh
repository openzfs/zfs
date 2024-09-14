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
# 	Verify mixed Direct I/O and mmap I/O.
#
# STRATEGY:
#	1. Create an empty file.
#	2. Start a background Direct I/O random read/write fio to the
#	   file.
#	3. Start a background mmap random read/write fio to the file.
#

verify_runnable "global"

function cleanup
{
	zfs set recordsize=$rs $TESTPOOL/$TESTFS
	log_must rm -f "$tmp_file"
}

log_assert "Verify mixed Direct I/O and mmap I/O"

log_onexit cleanup

mntpnt=$(get_prop mountpoint $TESTPOOL/$TESTFS)
tmp_file=$mntpnt/file
bs=$((128 * 1024))
blocks=64
size=$((bs * blocks))
runtime=60

rs=$(get_prop recordsize $TESTPOOL/$TESTFS)
log_must zfs set recordsize=128k $TESTPOOL/$TESTFS

log_must stride_dd -i /dev/zero -o $tmp_file -b $bs -c $blocks

# Direct I/O writes
log_must eval "fio --filename=$tmp_file --name=direct-write \
	--rw=randwrite --size=$size --bs=$bs --direct=1 --numjobs=1 \
	--ioengine=sync --fallocate=none --group_reporting --minimal \
	--runtime=$runtime --time_based --norandommap &"

# Direct I/O reads
log_must eval "fio --filename=$tmp_file --name=direct-read \
	--rw=randread --size=$size --bs=$bs --direct=1 --numjobs=1 \
	--ioengine=sync --fallocate=none --group_reporting --minimal \
	--runtime=$runtime --time_based --norandommap &"

# mmap I/O writes
log_must eval "fio --filename=$tmp_file --name=mmap-write \
	--rw=randwrite --size=$size --bs=$bs --numjobs=1 \
	--ioengine=mmap --fallocate=none --group_reporting --minimal \
	--runtime=$runtime --time_based --norandommap &"

# mmap I/O reads
log_must eval "fio --filename=$tmp_file --name=mmap-read \
	--rw=randread --size=$size --bs=$bs --numjobs=1 \
	--ioengine=mmap --fallocate=none --group_reporting --minimal \
	--runtime=$runtime --time_based --norandommap &"

wait

log_pass "Verfied mixed Direct I/O and mmap I/O"
