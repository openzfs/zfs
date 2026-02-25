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
# Copyright (c) 2026 Klara, Inc.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Launch multiple concurrent writers to an anymirror1 AnyRAID pool,
# then verify all data reads back correctly after export/import.
# This exercises AnyRAID under concurrent I/O pressure.
#
# STRATEGY:
# 1. Create anymirror1 pool with 4 disks.
# 2. Set ANYRAID_MIN_TILE_SIZE to 64MiB.
# 3. Launch 8 background file_write processes writing to separate files.
# 4. Wait for all writers to complete.
# 5. Record xxh128 checksums of all files.
# 6. Export and re-import the pool.
# 7. Verify all checksums match.
# 8. Run scrub, verify no errors.
#

verify_runnable "global"

NUM_WRITERS=8
FILE_SIZE_MB=32

cleanup() {
	zpool destroy $TESTPOOL
	set_tunable64 ANYRAID_MIN_TILE_SIZE 1073741824
	rm -f $TEST_BASE_DIR/vdev_file.{0,1,2,3}
}

log_onexit cleanup

log_must truncate -s 1G $TEST_BASE_DIR/vdev_file.{0,1,2,3}

set_tunable64 ANYRAID_MIN_TILE_SIZE 67108864

log_assert "Concurrent writes to anymirror1 pool preserve data correctly across export/import"

log_must create_pool $TESTPOOL anymirror1 \
	$TEST_BASE_DIR/vdev_file.{0,1,2,3}

#
# Launch concurrent writers. Each writes a 32MiB file with random data.
#
typeset -i idx=0
while (( idx < NUM_WRITERS )); do
	file_write -o create -b 1048576 -c $FILE_SIZE_MB -d 'R' \
		-f /$TESTPOOL/concurrent.$idx &
	(( idx = idx + 1 ))
done

wait

#
# Verify all files exist and record checksums.
#
set -A cksums
idx=0
while (( idx < NUM_WRITERS )); do
	if [[ ! -f /$TESTPOOL/concurrent.$idx ]]; then
		log_fail "File concurrent.$idx was not created by background writer"
	fi
	cksums[$idx]=$(xxh128digest /$TESTPOOL/concurrent.$idx)
	(( idx = idx + 1 ))
done

#
# Export and re-import the pool.
#
log_must zpool sync $TESTPOOL
log_must zpool export $TESTPOOL
log_must zpool import -d $TEST_BASE_DIR $TESTPOOL

#
# Verify all checksums after import.
#
idx=0
while (( idx < NUM_WRITERS )); do
	newcksum=$(xxh128digest /$TESTPOOL/concurrent.$idx)
	[[ "$newcksum" == "${cksums[$idx]}" ]] || \
		log_fail "Checksum mismatch for concurrent.$idx: expected=${cksums[$idx]} got=$newcksum"
	(( idx = idx + 1 ))
done

#
# Run scrub and verify no errors.
#
log_must zpool scrub $TESTPOOL
log_must zpool wait -t scrub $TESTPOOL

log_must check_pool_status $TESTPOOL state ONLINE true
log_must is_pool_scrubbed $TESTPOOL true

cksum_count=$(zpool status -v $TESTPOOL | grep ONLINE | awk 'NF > 2 && $5 != 0' | wc -l)
(( cksum_count == 0 )) || log_fail "checksum errors detected after scrub"

log_pass "Concurrent writes to anymirror1 pool preserve data correctly across export/import"
