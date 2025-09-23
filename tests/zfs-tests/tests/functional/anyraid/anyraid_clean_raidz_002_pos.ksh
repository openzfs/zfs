#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
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
# Copyright (c) 2026, Klara, Inc.
#

. $STF_SUITE/tests/functional/anyraid/anyraid_common.kshlib

#
# DESCRIPTION:
# AnyRAID raidz2:2 can survive having 1-2 failed disks. This is the raidz
# equivalent of clean_mirror_002. With parity=2, the pool should tolerate
# up to 2 simultaneous disk failures without data loss.
#
# STRATEGY:
# 1. Create an anyraidz2:2 pool with 5 disks.
# 2. Write files and record xxh128 checksums.
# 3. Punch holes in 1 or 2 disks at a time.
# 4. Export/import, verify all checksums match.
# 5. Scrub and verify no errors.
# 6. Repeat for each disk failure combination.
#

verify_runnable "global"

function cleanup
{
	poolexists $TESTPOOL && destroy_pool $TESTPOOL
	log_must delete_sparse_files
}

log_onexit cleanup

log_assert "AnyRAID raidz2:2 can survive having 1-2 failed disks"

log_must create_sparse_files "disk" 5 $DEVSIZE

typeset poolspec="anyraidz2:2 $disk0 $disk1 $disk2 $disk3 $disk4"
typeset diskdir=$(dirname $disk0)

#
# Build the list of failure cases: all single-disk and all 2-disk
# combinations from 5 disks.
#
set -A all_disks $disk0 $disk1 $disk2 $disk3 $disk4
set -A fail_cases
typeset -i case_idx=0

# Single-disk failures (5 cases).
typeset -i i=0
while (( i < 5 )); do
	fail_cases[$case_idx]="${all_disks[$i]}"
	(( case_idx = case_idx + 1 ))
	(( i = i + 1 ))
done

# 2-disk failure combinations (10 cases).
i=0
while (( i < 4 )); do
	typeset -i j
	(( j = i + 1 ))
	while (( j < 5 )); do
		fail_cases[$case_idx]="${all_disks[$i]} ${all_disks[$j]}"
		(( case_idx = case_idx + 1 ))
		(( j = j + 1 ))
	done
	(( i = i + 1 ))
done

log_note "Total failure cases to test: $case_idx"

typeset -i case_num=0
while (( case_num < case_idx )); do
	typeset tcase="${fail_cases[$case_num]}"
	log_note "Test case $(( case_num + 1 ))/$case_idx: failing disks: $tcase"

	log_must zpool create -f $TESTPOOL $poolspec

	#
	# Write files and record checksums.
	#
	typeset -i atfile=0
	set -A files
	set -A cksums
	typeset newcksum

	while (( atfile < FILE_COUNT )); do
		files[$atfile]=/$TESTPOOL/file.$atfile
		log_must file_write -o create -f ${files[$atfile]} \
			-b $FILE_SIZE -c 1
		cksums[$atfile]=$(xxh128digest ${files[$atfile]})
		(( atfile = atfile + 1 ))
	done

	#
	# Punch holes in the target disk(s) to simulate failure.
	#
	for failed_disk in $tcase; do
		log_must punch_hole $((DD_BLOCK * 8)) \
			$((DD_BLOCK * (DD_COUNT - 128))) $failed_disk
	done

	#
	# Flush out the cache by exporting and re-importing.
	#
	log_must zpool export $TESTPOOL
	log_must zpool import -d $diskdir $TESTPOOL

	#
	# Verify all file checksums match.
	#
	atfile=0
	typeset -i failedcount=0
	while (( atfile < FILE_COUNT )); do
		newcksum=$(xxh128digest ${files[$atfile]})
		if [[ $newcksum != ${cksums[$atfile]} ]]; then
			(( failedcount = failedcount + 1 ))
		fi
		(( atfile = atfile + 1 ))
	done

	if (( failedcount > 0 )); then
		log_fail "$failedcount of $FILE_COUNT files had wrong" \
			"checksums after failing disks: $tcase"
	fi

	#
	# Run scrub and verify no errors.
	#
	log_must zpool scrub $TESTPOOL
	log_must wait_scrubbed $TESTPOOL

	#
	# Destroy pool for the next iteration.
	#
	log_must destroy_pool $TESTPOOL

	(( case_num = case_num + 1 ))
done

log_pass "AnyRAID raidz2:2 can survive having 1-2 failed disks"
