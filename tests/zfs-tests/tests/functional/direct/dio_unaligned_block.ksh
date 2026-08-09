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
# 	Verify failure for (un)aligned O_DIRECT
#
# STRATEGY:
#	1. Create a multi-block file
#	2. Perform (un)aligned write/read verify the result.
#

verify_runnable "global"

log_must save_tunable DIO_STRICT
function cleanup
{
	restore_tunable DIO_STRICT
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

log_must set_tunable32 DIO_STRICT 0
log_must zfs set direct=standard $TESTPOOL/$TESTFS
# sub-pagesize direct writes/read will always pass if not strict.
log_must stride_dd -i /dev/urandom -o $tmp_file -b 512 -c 8 -D
log_must stride_dd -i $tmp_file -o /dev/null -b 512 -c 8 -d

log_must set_tunable32 DIO_STRICT 1
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
