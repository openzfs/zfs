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
# Copyright (c) 2023 by Lawrence Livermore National Security, LLC.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/mmap/mmap.cfg

#
# DESCRIPTION:
# 	Verify mixed buffered and mmap IO.
#
# STRATEGY:
#	1. Create an empty file.
#	2. Start a background buffered read/write fio to the file.
#	3. Start a background mmap read/write fio to the file.
#

verify_runnable "global"

function cleanup
{
	log_must rm -f "$tmp_file"
}

log_assert "Verify mixed buffered and mmap IO"

log_onexit cleanup

mntpnt=$(get_prop mountpoint $TESTPOOL/$TESTFS)
tmp_file=$mntpnt/file
bs=$((128 * 1024))
blocks=64
size=$((bs * blocks))
runtime=60

log_must dd if=/dev/zero of=$tmp_file bs=$bs count=$blocks

# Buffered IO writes
log_must eval "fio --filename=$tmp_file --name=buffer-write \
	--rw=randwrite --size=$size --bs=$bs --direct=0 --numjobs=1 \
	--ioengine=sync --fallocate=none --group_reporting --minimal \
	--runtime=$runtime --time_based --norandommap &"

# Buffered IO reads
log_must eval "fio --filename=$tmp_file --name=buffer-read \
	--rw=randread --size=$size --bs=$bs --direct=0 --numjobs=1 \
	--ioengine=sync --fallocate=none --group_reporting --minimal \
	--runtime=$runtime --time_based --norandommap &"

# mmap IO writes
log_must eval "fio --filename=$tmp_file --name=mmap-write \
	--rw=randwrite --size=$size --bs=$bs --numjobs=1 \
	--ioengine=mmap --fallocate=none --group_reporting --minimal \
	--runtime=$runtime --time_based --norandommap &"

# mmap IO reads
log_must eval "fio --filename=$tmp_file --name=mmap-read \
	--rw=randread --size=$size --bs=$bs --numjobs=1 \
	--ioengine=mmap --fallocate=none --group_reporting --minimal \
	--runtime=$runtime --time_based --norandommap &"

log_must wait

log_pass "Verfied mixed buffered and mmap IO"
