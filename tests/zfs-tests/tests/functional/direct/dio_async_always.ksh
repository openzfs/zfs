#!/bin/ksh -p
#
# DDL HEADER START
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
