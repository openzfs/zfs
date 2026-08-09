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
# Note that the "-D" and "-d" flags are not set in the following calls to
# stride_dd.
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
# Note that the "-D" and "-d" flags are always set in the following calls to
# stride_dd.
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
