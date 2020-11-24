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
# 	Verify the direct=always|disabled|standard property
#
# STRATEGY:
#	1. Verify direct=always behavior
#	2. Verify direct=disabled behavior
#	3. Verify direct=standard behavior
#

verify_runnable "global"

function cleanup
{
	zfs set direct=standard $TESTPOOL/$TESTFS
	log_must rm -f $tmp_file
	check_dio_write_chksum_verify_failures $TESTPOOL "raidz" 0
}

log_assert "Verify the direct=always|disabled|standard property"

log_onexit cleanup

mntpnt=$(get_prop mountpoint $TESTPOOL/$TESTFS)
rs=$(get_prop recordsize $TESTPOOL/$TESTFS)

tmp_file=$mntpnt/tmp_file
page_size=$(getconf PAGESIZE)
file_size=1048576
count=8

#
# Check when "direct=always" any aligned IO is done as direct.
# Note that "flag=direct" is not set in the following calls to dd(1).
#
log_must zfs set direct=always $TESTPOOL/$TESTFS

log_note "Aligned writes (buffered, then all direct)"
check_write $TESTPOOL $tmp_file $rs $count 0 "" 1 $((count - 1))

log_note "Aligned overwrites"
check_write $TESTPOOL $tmp_file $rs $count 0 "" 0 $count

log_note "Sub-recordsize unaligned overwrites"
check_write $TESTPOOL $tmp_file $((rs / 2)) $((2 * count)) 0 "" $((2 * count)) 0

log_note "Sub-page size aligned overwrites"
check_write $TESTPOOL $tmp_file 512 $count 0 "" $count 0
evict_blocks $TESTPOOL $tmp_file $file_size

log_note "Aligned reads"
check_read $TESTPOOL $tmp_file $rs $count 0 "" 0 $count

log_note "Sub-recordsize unaligned reads"
check_read $TESTPOOL $tmp_file $((rs / 2)) $((count * 2)) 0 "" 0 $((2 * count))

log_note "Sub-page size aligned reads (one read then ARC hits)"
check_read $TESTPOOL $tmp_file 512 $count 0 "" 1 0

log_must rm -f $tmp_file


#
# Check when "direct=disabled" there are never any direct requests.
# Note that "flag=direct" is always set in the following calls to dd(1).
#
log_must zfs set direct=disabled $TESTPOOL/$TESTFS

log_note "Aligned writes (all buffered with an extra for create)"
check_write $TESTPOOL $tmp_file $rs $count 0 "-D" $count 0

log_note "Aligned overwrites"
check_write $TESTPOOL $tmp_file $rs $count 0 "-D" $count 0

log_note "Aligned reads (all ARC hits)"
check_read $TESTPOOL $tmp_file $rs $count 0 "-d" 0 0

log_must rm -f $tmp_file


#
# Check when "direct=standard" only requested Direct I/O occur.
#
log_must zfs set direct=standard $TESTPOOL/$TESTFS

log_note "Aligned writes/overwrites (buffered / direct)"
check_write $TESTPOOL $tmp_file $rs $count 0 "" $count 0
check_write $TESTPOOL $tmp_file $rs $count 0 "-D" 0 $count

log_note "Aligned reads (buffered / direct)"
evict_blocks $TESTPOOL $tmp_file $file_size
check_read $TESTPOOL $tmp_file $rs $count 0 "" $count 0
evict_blocks $TESTPOOL $tmp_file $file_size
check_read $TESTPOOL $tmp_file $rs $count 0 "-d" 0 $count

log_pass "Verify the direct=always|disabled|standard property"
