#! /bin/ksh -p
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
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
# Copyright (c) 2018 by Lawrence Livermore National Security, LLC.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/io/io.cfg

#
# DESCRIPTION:
#	Verify Linux native asynchronous IO.
#
# STRATEGY:
#	1. Use fio(1) in verify mode to perform write, read,
#	   random read, and random write workloads.
#	2. Repeat the test with additional fio(1) options.
#

verify_runnable "global"

function cleanup
{
	log_must rm -f "$mntpnt/rw*"
}

log_assert "Verify Linux native asynchronous IO"

log_onexit cleanup

ioengine="--ioengine=libaio"
mntpnt=$(get_prop mountpoint $TESTPOOL/$TESTFS)
dir="--directory=$mntpnt"

set -A fio_arg -- "--sync=0" "--sync=1" "--direct=0" "--direct=1"

for arg in "${fio_arg[@]}"; do
	log_must fio $dir $ioengine $arg $FIO_WRITE_ARGS
	log_must fio $dir $ioengine $arg $FIO_READ_ARGS
	log_must fio $dir $ioengine $arg $FIO_RANDWRITE_ARGS
	log_must fio $dir $ioengine $arg $FIO_RANDREAD_ARGS
	log_must rm -f "$mntpnt/rw*"
done

log_pass "Verified Linux native asynchronous IO"
