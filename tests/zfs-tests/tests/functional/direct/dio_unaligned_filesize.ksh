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
	check_dio_write_chksum_verify_failures $TESTPOOL "raidz" 0
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
prev_dio_read=$(get_iostats_stat $TESTPOOL direct_read_count)
prev_arc_read=$(get_iostats_stat $TESTPOOL arc_read_count)
log_must stride_dd -i $filename -o /dev/null -b $bs -e -d
curr_dio_read=$(get_iostats_stat $TESTPOOL direct_read_count)
curr_arc_read=$(get_iostats_stat $TESTPOOL arc_read_count)
total_dio_read=$((curr_dio_read - prev_dio_read))
total_arc_read=$((curr_arc_read - prev_arc_read))

# We should see both Direct I/O reads an ARC read to read the entire file that
# is not page-size aligned
if [[ $total_dio_read -lt 2 ]] || [[ $total_arc_read -lt 1 ]]; then
	log_fail "Expect 2 reads from Direct I/O and 1 from the ARC but \
	    Direct I/O: $total_dio_read ARC: $total_arc_read"
fi

log_pass "Verified Direct I/O read can read a none page-aligned length file"
