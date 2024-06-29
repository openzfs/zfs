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
	check_dio_write_chksum_verify_failures $TESTPOOL "raidz" 0
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
