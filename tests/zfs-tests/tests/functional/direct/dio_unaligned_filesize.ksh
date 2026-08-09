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
# Copyright (c) 2022 by Triad National Security, LLC.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/direct/dio.cfg
. $STF_SUITE/tests/functional/direct/dio.kshlib

#
# DESCRIPTION:
#	Verify Direct I/O reads can read an entire file that is not
#	page-aligned in length. When a file is not page-aligned in total
#	length, as much that can be read using using O_DIRECT is done so and
#	the rest is read using the ARC. O_DIRECT requires page-size alignment.
#
# STRATEGY:
#	1. Write a file that is page-aligned (buffered)
#	2. Truncate the file to be 512 bytes less
#	3. Export then import the Zpool flushing out the ARC
#	4. Read back the file using O_DIRECT
#	5. Verify the file is read back with both Direct I/O and buffered I/O
#

verify_runnable "global"

function cleanup
{
	log_must rm -f "$filename"
	log_must set recordsize=$rs $TESTPOOL/$TESTFS
}

log_assert "Verify Direct I/O reads can read an entire file that is not \
    page-aligned"

log_onexit cleanup

mntpnt=$(get_prop mountpoint $TESTPOOL/$TESTFS)

rs=$(get_prop recordsize $TESTPOOL/$TESTFS)
log_must zfs set recordsize=128k $TESTPOOL/$TESTFS

bs=$((128 * 1024)) # bs=recordsize (128k)
filename="$mntpnt/testfile.iso"

log_must stride_dd -i /dev/urandom -o $filename -b $bs -c 2 
# Truncating file so the total length is no longer page-size aligned
log_must do_truncate_reduce $filename 512

# Exporting the Zpool to make sure all future reads happen from the ARC
log_must zpool export $TESTPOOL
log_must zpool import $TESTPOOL

# Reading the file back using Direct I/O
prev_dio_read=$(kstat_pool $TESTPOOL iostats.direct_read_count)
prev_arc_read=$(kstat_pool $TESTPOOL iostats.arc_read_count)
log_must stride_dd -i $filename -o /dev/null -b $bs -e -d
curr_dio_read=$(kstat_pool $TESTPOOL iostats.direct_read_count)
curr_arc_read=$(kstat_pool $TESTPOOL iostats.arc_read_count)
total_dio_read=$((curr_dio_read - prev_dio_read))
total_arc_read=$((curr_arc_read - prev_arc_read))

# We should see both Direct I/O reads an ARC read to read the entire file that
# is not page-size aligned
if [[ $total_dio_read -lt 2 ]] || [[ $total_arc_read -lt 1 ]]; then
	log_fail "Expect 2 reads from Direct I/O and 1 from the ARC but \
	    Direct I/O: $total_dio_read ARC: $total_arc_read"
fi

log_pass "Verified Direct I/O read can read a none page-aligned length file"
