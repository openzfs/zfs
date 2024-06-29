#!/bin/ksh -p
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
	check_dio_write_chksum_verify_failures $TESTPOOL "raidz" 0
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
