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
# 	Verify the number direct/buffered requests for (un)aligned access
#
# STRATEGY:
#	1. Create a multi-block file
#	2. Perform various (un)aligned accesses and verify the result.
#

verify_runnable "global"

function cleanup
{
	zfs set recordsize=$rs $TESTPOOL/$TESTFS
	log_must rm -f $tmp_file
}

log_onexit cleanup

log_assert "Verify the number direct/buffered requests for unaligned access"

mntpnt=$(get_prop mountpoint $TESTPOOL/$TESTFS)

rs=$(get_prop recordsize $TESTPOOL/$TESTFS)
log_must zfs set recordsize=128k $TESTPOOL/$TESTFS

tmp_file=$mntpnt/tmp_file
file_size=$((rs * 8))

log_must stride_dd -i /dev/urandom -o $tmp_file -b $file_size -c 1

# N recordsize aligned writes which do not span blocks
check_write $TESTPOOL $tmp_file $rs 1 0 "-D" 0 1
check_write $TESTPOOL $tmp_file $rs 2 0 "-D" 0 2
check_write $TESTPOOL $tmp_file $rs 4 0 "-D" 0 4
check_write $TESTPOOL $tmp_file $rs 8 0 "-D" 0 8

# 1 recordsize aligned write which spans multiple blocks at various offsets
check_write $TESTPOOL $tmp_file $((rs * 2)) 1 0 "-D" 0 2
check_write $TESTPOOL $tmp_file $((rs * 2)) 1 1 "-D" 0 2
check_write $TESTPOOL $tmp_file $((rs * 2)) 1 2 "-D" 0 2
check_write $TESTPOOL $tmp_file $((rs * 2)) 1 3 "-D" 0 2
check_write $TESTPOOL $tmp_file $((rs * 4)) 1 0 "-D" 0 4
check_write $TESTPOOL $tmp_file $((rs * 4)) 1 1 "-D" 0 4
check_write $TESTPOOL $tmp_file $((rs * 8)) 1 0 "-D" 0 8

# sub-blocksize unaligned writes which do not span blocks.
check_write $TESTPOOL $tmp_file $((rs / 2)) 1 0 "-D" 1 0
check_write $TESTPOOL $tmp_file $((rs / 2)) 1 1 "-D" 1 0
check_write $TESTPOOL $tmp_file $((rs / 2)) 1 2 "-D" 1 0
check_write $TESTPOOL $tmp_file $((rs / 2)) 1 3 "-D" 1 0

# large unaligned writes which span multiple blocks
check_write $TESTPOOL $tmp_file $((rs * 2)) 1 $((rs / 2)) "-D -K" 2 1
check_write $TESTPOOL $tmp_file $((rs * 4)) 2 $((rs / 4)) "-D -K" 4 6

# evict any cached blocks by overwriting with O_DIRECT
evict_blocks $TESTPOOL $tmp_file $file_size

# recordsize aligned reads which do not span blocks
check_read $TESTPOOL $tmp_file $rs 1 0 "-d" 0 1
check_read $TESTPOOL $tmp_file $rs 2 0 "-d" 0 2
check_read $TESTPOOL $tmp_file $rs 4 0 "-d" 0 4
check_read $TESTPOOL $tmp_file $rs 8 0 "-d" 0 8

# 1 recordsize aligned read which spans multiple blocks at various offsets
check_read $TESTPOOL $tmp_file $((rs * 2)) 1 0 "-d" 0 2
check_read $TESTPOOL $tmp_file $((rs * 2)) 1 1 "-d" 0 2
check_read $TESTPOOL $tmp_file $((rs * 2)) 1 2 "-d" 0 2
check_read $TESTPOOL $tmp_file $((rs * 2)) 1 3 "-d" 0 2
check_read $TESTPOOL $tmp_file $((rs * 4)) 1 0 "-d" 0 4
check_read $TESTPOOL $tmp_file $((rs * 4)) 1 1 "-d" 0 4
check_read $TESTPOOL $tmp_file $((rs * 8)) 1 0 "-d" 0 8

# sub-blocksize unaligned reads which do not span blocks.
check_read $TESTPOOL $tmp_file $((rs / 2)) 1 0 "-d" 0 1
check_read $TESTPOOL $tmp_file $((rs / 2)) 1 1 "-d" 0 1
check_read $TESTPOOL $tmp_file $((rs / 2)) 1 2 "-d" 0 1
check_read $TESTPOOL $tmp_file $((rs / 2)) 1 3 "-d" 0 1

# large unaligned reads which span multiple blocks
check_read $TESTPOOL $tmp_file $((rs * 2)) 1 $((rs / 2)) "-d -P" 0 3
check_read $TESTPOOL $tmp_file $((rs * 4)) 1 $((rs / 4)) "-d -P" 0 5

log_pass "Verify the number direct/buffered requests for (un)aligned access"
