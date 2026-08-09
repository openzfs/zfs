#! /bin/ksh -p
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
# Copyright (c) 2018 by Lawrence Livermore National Security, LLC.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/io/io.cfg

#
# DESCRIPTION:
#	Verify basic read(2), write(2) and lseek(2).
#
# STRATEGY:
#	1. Use fio(1) in verify mode to perform write, read,
#	   random read, and random write workloads.
#	2. Repeat the test with additional fio(1) options.
#

verify_runnable "global"

command -v fio > /dev/null || log_unsupported "fio missing"

function cleanup
{
	log_must rm -f "$mntpnt/rw*"
}

log_assert "Verify basic read(2), write(2) and lseek(2)"

log_onexit cleanup

ioengine="--ioengine=sync"
mntpnt=$(get_prop mountpoint $TESTPOOL/$TESTFS)
dir="--directory=$mntpnt"

set -A fio_arg -- "--sync=0" "--sync=1" "--direct=0" "--direct=1"

for arg in ${fio_arg[@]}; do
	log_must fio $dir $ioengine $arg $FIO_WRITE_ARGS
	log_must fio $dir $ioengine $arg $FIO_READ_ARGS
	log_must fio $dir $ioengine $arg $FIO_RANDWRITE_ARGS
	log_must fio $dir $ioengine $arg $FIO_RANDREAD_ARGS
	log_must rm -f "$mntpnt/rw*"
done

log_pass "Verified basic read(2), write(2) and lseek(2)"
