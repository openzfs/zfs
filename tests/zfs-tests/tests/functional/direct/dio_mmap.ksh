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
