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
# 	Verify the number direct/buffered requests when growing a file
#
# STRATEGY:
#

verify_runnable "global"

function cleanup
{
	zfs set recordsize=$rs $TESTPOOL/$TESTFS
	log_must rm -f $tmp_file
}

log_assert "Verify the number direct/buffered requests when growing a file"

log_onexit cleanup

mntpnt=$(get_prop mountpoint $TESTPOOL/$TESTFS)

tmp_file=$mntpnt/tmp_file

rs=$(get_prop recordsize $TESTPOOL/$TESTFS)
log_must zfs set recordsize=128k $TESTPOOL/$TESTFS

#
# Verify the expected number of buffered and Direct I/O's when growing
# the first block of a file up to the maximum recordsize.
#
for bs in "8192" "16384" "32768" "65536" "131072"; do

	# When O_DIRECT is set the first write to a new file, or when the
	# block size needs to be grown, it will be done as a buffered write.
	check_write $TESTPOOL $tmp_file $bs 1 0 "-D" 1 0

	# Overwriting the first block of an existing file with O_DIRECT will
	# be a buffered write if less than the block size.
	check_write $TESTPOOL $tmp_file 4096 1 0 "-D" 1 0
	check_write $TESTPOOL $tmp_file 4096 1 1 "-D" 1 0

	# Overwriting the first block of an existing file with O_DIRECT will
	# be a direct write as long as the block size matches.
	check_write $TESTPOOL $tmp_file $bs 1 0 "-D" 0 1

	# Evict any blocks which may be buffered before the read tests.
	evict_blocks $TESTPOOL $tmp_file $bs

	# Reading the first block of an existing file with O_DIRECT will
	# be a direct read for part or all of the block size.
	check_read $TESTPOOL $tmp_file $bs 1 0 "-d" 0 1
	check_read $TESTPOOL $tmp_file 4096 1 0 "-d" 0 1
	check_read $TESTPOOL $tmp_file 4096 1 1 "-d" 0 1
done

log_pass "Verify the number direct/buffered requests when growing a file"
