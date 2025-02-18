#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
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
