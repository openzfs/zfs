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
# 	Verify stable pages work for O_DIRECT writes.
#
# STRATEGY:
#	1. Start a Direct I/O write workload while manipulating the user
#	   buffer.
#	2. Verify we can Read the contents of the file using buffered reads.
#	3. Verify there is no checksum errors reported from zpool status.
#	4. Repeat steps 1 and 2 for 3 iterations.
#	5. Repeat 1-3 but with compression disabled.
#

verify_runnable "global"

function cleanup
{
	log_must rm -f "$mntpnt/direct-write.iso"
	check_dio_write_chksum_verify_failures $TESTPOOL "raidz" 0
}

log_assert "Verify stable pages work for Direct I/O writes."

if is_linux; then
	log_unsupported "Linux does not support stable pages for O_DIRECT \
	     writes"
fi

log_onexit cleanup

ITERATIONS=3
NUMBLOCKS=300
BS=$((128 * 1024)) #128k
mntpnt=$(get_prop mountpoint $TESTPOOL/$TESTFS)
log_must zfs set recordsize=128k $TESTPOOL/$TESTFS

for compress in "on" "off";
do
	log_must zfs set compression=$compress $TESTPOOL/$TESTFS

	for i in $(seq 1 $ITERATIONS); do
		log_note "Verifying stable pages for Direct I/O writes \
		    iteration $i of $ITERATIONS"

		prev_dio_wr=$(get_iostats_stat $TESTPOOL direct_write_count)

		# Manipulate the user's buffer while running O_DIRECT write
		# workload with the buffer.
		log_must manipulate_user_buffer -o "$mntpnt/direct-write.iso" \
		    -n $NUMBLOCKS -b $BS 

		# Reading back the contents of the file
		log_must stride_dd -i $mntpnt/direct-write.iso -o /dev/null \
		    -b $BS -c $NUMBLOCKS

		curr_dio_wr=$(get_iostats_stat $TESTPOOL direct_write_count)
		total_dio_wr=$((curr_dio_wr - prev_dio_wr))

		log_note "Making sure we have Direct I/O writes logged"
		if [[ $total_dio_wr -lt 1 ]]; then
			log_fail "No Direct I/O writes $total_dio_wr"
		fi

		# Making sure there are no data errors for the zpool
		log_note "Making sure there are no checksum errors with the ZPool"
		log_must check_pool_status $TESTPOOL "errors" \
		    "No known data errors"

		log_must rm -f "$mntpnt/direct-write.iso"
	done
done

log_pass "Verified stable pages work for Direct I/O writes."
