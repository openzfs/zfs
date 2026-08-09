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
# Copyright (c) 2023 by Triad National Security, LLC.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/direct/dio.cfg
. $STF_SUITE/tests/functional/direct/dio.kshlib

#
# DESCRIPTION:
# 	Verify Direct I/O overwrite.
#
# STRATEGY:
#	1. Create an empty file.
#	2. Start a Direct I/O random write fio to the file.
#

verify_runnable "global"

function cleanup
{
	zfs set recordsize=$rs $TESTPOOL/$TESTFS
	log_must rm -f "$tmp_file"
}

log_assert "Verify Direct I/O overwrites"

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

# Direct I/O overwrites
log_must eval "fio --filename=$tmp_file --name=direct-write \
	--rw=randwrite --size=$size --bs=$bs --direct=1 --numjobs=1 \
	--ioengine=sync --fallocate=none --group_reporting --minimal \
	--runtime=$runtime --time_based --norandommap"

log_pass "Verfied Direct I/O overwrites"
