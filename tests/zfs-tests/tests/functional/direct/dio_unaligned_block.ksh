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
# Copyright (c) 2021 by Lawrence Livermore National Security, LLC.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/direct/dio.cfg
. $STF_SUITE/tests/functional/direct/dio.kshlib

#
# DESCRIPTION:
# 	Verify failure for (un)aligned O_DIRECT
#
# STRATEGY:
#	1. Create a multi-block file
#	2. Perform (un)aligned write/read verify the result.
#

verify_runnable "global"

function cleanup
{
	zfs set recordsize=$rs $TESTPOOL/$TESTFS
	zfs set direct=standard $TESTPOOL/$TESTFS
	log_must rm -f $tmp_file
}

log_onexit cleanup

log_assert "Verify direct requests for (un)aligned access"

mntpnt=$(get_prop mountpoint $TESTPOOL/$TESTFS)

rs=$(get_prop recordsize $TESTPOOL/$TESTFS)
log_must zfs set recordsize=128k $TESTPOOL/$TESTFS

tmp_file=$mntpnt/tmp_file
file_size=$((rs * 8))

log_must stride_dd -i /dev/urandom -o $tmp_file -b $file_size -c 1

log_must zfs set direct=standard $TESTPOOL/$TESTFS
# sub-pagesize direct writes/read will always fail if direct=standard.
log_mustnot stride_dd -i /dev/urandom -o $tmp_file -b 512 -c 8 -D
log_mustnot stride_dd -i $tmp_file -o /dev/null -b 512 -c 8 -d

log_must zfs set direct=always $TESTPOOL/$TESTFS
# sub-pagesize direct writes/read will always pass if direct=always.
log_must stride_dd -i /dev/urandom -o $tmp_file -b 512 -c 8
log_must stride_dd -i $tmp_file -o /dev/null -b 512 -c 8

log_must zfs set direct=disabled $TESTPOOL/$TESTFS
# sub-pagesize direct writes/read will always pass if direct=disabled.
log_must stride_dd -i /dev/urandom -o $tmp_file -b 512 -c 8 -D
log_must stride_dd -i $tmp_file -o /dev/null -b 512 -c 8 -d

log_pass "Verify direct requests for (un)aligned access"
