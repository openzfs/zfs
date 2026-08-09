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
# 	Verify small async Direct I/O requests
#
# STRATEGY:
#	1. Use fio to issue small read/write requests.  Writes are
#	   smaller than the block size and thus will be buffered,
#	   reads satisfy the minimum alignment and will be direct.
#

verify_runnable "global"

function cleanup
{
	zfs set direct=standard $TESTPOOL/$TESTFS
	rm $tmp_file
}

log_assert "Verify direct=always mixed small async requests"

log_onexit cleanup

log_must zfs set direct=always $TESTPOOL/$TESTFS

mntpnt=$(get_prop mountpoint $TESTPOOL/$TESTFS)
tmp_file=$mntpnt/tmp_file
page_size=$(getconf PAGESIZE)
file_size=1G
runtime=10

log_must truncate -s $file_size $tmp_file

log_must fio --filename=$tmp_file --name=always-randrw \
        --rw=randwrite --bs=$page_size --size=$file_size --numjobs=1 \
	    --ioengine=posixaio --fallocate=none --iodepth=4 --verify=sha1 \
        --group_reporting --minimal --runtime=$runtime --time_based

log_pass "Verify direct=always mixed small async requests"
