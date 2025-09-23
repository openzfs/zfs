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
# AnyRAID raidz1:2 can survive having 1 failed disk. This is the raidz
# equivalent of the clean_mirror tests. With parity=1, the pool should
# tolerate any single disk failure without data loss.
#
# STRATEGY:
# 1. Create an anyraidz1:2 pool with 4 disks.
# 2. Write files and record xxh128 checksums.
# 3. Punch holes in 1 disk at a time.
# 4. Export/import, verify all checksums match.
# 5. Scrub and verify no errors.
# 6. Repeat for each disk individually.
#

verify_runnable "global"

function cleanup
{
	poolexists $TESTPOOL && destroy_pool $TESTPOOL
	log_must delete_sparse_files
}

log_onexit cleanup

log_assert "AnyRAID raidz1:2 can survive having 1 failed disk"

log_must create_sparse_files "disk" 4 $DEVSIZE

typeset poolspec="anyraidz1:2 $disk0 $disk1 $disk2 $disk3"
typeset diskdir=$(dirname $disk0)

#
# Test each single-disk failure case individually.
#
for failed_disk in $disk0 $disk1 $disk2 $disk3; do
	log_note "Testing single-disk failure: $failed_disk"

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
	# Punch a hole in the target disk to simulate failure.
	#
	log_must punch_hole $((DD_BLOCK * 8)) \
		$((DD_BLOCK * (DD_COUNT - 128))) $failed_disk

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
			"checksums after failing disk $failed_disk"
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
done

log_pass "AnyRAID raidz1:2 can survive having 1 failed disk"
