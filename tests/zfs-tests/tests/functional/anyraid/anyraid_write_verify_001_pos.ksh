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
# Write various data patterns to an anymirror1 AnyRAID pool and verify
# they read back correctly after export/import. This tests basic
# AnyRAID mirror-style data preservation.
#
# STRATEGY:
# 1. Create pool with anymirror1 (3 disks)
# 2. Write multiple files with different patterns (random, zeros,
#    known byte patterns) and varied sizes
# 3. Record xxh128 checksums of all files
# 4. Export and re-import the pool
# 5. Verify all checksums match
# 6. Run scrub, verify no errors
#

verify_runnable "global"

cleanup() {
	zpool destroy $TESTPOOL
	set_tunable64 ANYRAID_MIN_TILE_SIZE 1073741824
	rm -f $TEST_BASE_DIR/vdev_file.{0,1,2}
}

log_onexit cleanup

log_must truncate -s 1G $TEST_BASE_DIR/vdev_file.{0,1,2}

set_tunable64 ANYRAID_MIN_TILE_SIZE 67108864

log_assert "anymirror1 pool preserves data correctly across export/import"

log_must create_pool $TESTPOOL anymirror1 \
	$TEST_BASE_DIR/vdev_file.{0,1,2}

#
# Write files of varied sizes and patterns, record checksums.
#
set -A cksums
typeset -i cksum_idx=0

#
# Small 4K files with random data (10 files).
#
typeset -i idx=0
while (( idx < 10 )); do
	log_must file_write -o create -b 4096 -c 1 -d 'R' \
		-f /$TESTPOOL/small_random.$idx
	cksums[$cksum_idx]=$(xxh128digest /$TESTPOOL/small_random.$idx)
	(( cksum_idx = cksum_idx + 1 ))
	(( idx = idx + 1 ))
done

#
# Medium 1MiB files with random data (5 files).
#
idx=0
while (( idx < 5 )); do
	log_must file_write -o create -b 1048576 -c 1 -d 'R' \
		-f /$TESTPOOL/medium_random.$idx
	cksums[$cksum_idx]=$(xxh128digest /$TESTPOOL/medium_random.$idx)
	(( cksum_idx = cksum_idx + 1 ))
	(( idx = idx + 1 ))
done

#
# Large 128MiB file with random data (1 file).
#
log_must file_write -o create -b 1048576 -c 128 -d 'R' \
	-f /$TESTPOOL/large_random.0
cksums[$cksum_idx]=$(xxh128digest /$TESTPOOL/large_random.0)
(( cksum_idx = cksum_idx + 1 ))

#
# Zero-filled files (3 files at 64K each).
#
idx=0
while (( idx < 3 )); do
	log_must dd if=/dev/zero of=/$TESTPOOL/zeros.$idx \
		bs=65536 count=1 2>/dev/null
	cksums[$cksum_idx]=$(xxh128digest /$TESTPOOL/zeros.$idx)
	(( cksum_idx = cksum_idx + 1 ))
	(( idx = idx + 1 ))
done

typeset -i total_files=$cksum_idx

#
# Export and re-import the pool.
#
log_must zpool sync $TESTPOOL
log_must zpool export $TESTPOOL
log_must zpool import -d $TEST_BASE_DIR $TESTPOOL

#
# Verify all checksums after import.
#
cksum_idx=0

# Verify small random files.
idx=0
while (( idx < 10 )); do
	newcksum=$(xxh128digest /$TESTPOOL/small_random.$idx)
	[[ "$newcksum" == "${cksums[$cksum_idx]}" ]] || \
		log_fail "Checksum mismatch for small_random.$idx: expected=${cksums[$cksum_idx]} got=$newcksum"
	(( cksum_idx = cksum_idx + 1 ))
	(( idx = idx + 1 ))
done

# Verify medium random files.
idx=0
while (( idx < 5 )); do
	newcksum=$(xxh128digest /$TESTPOOL/medium_random.$idx)
	[[ "$newcksum" == "${cksums[$cksum_idx]}" ]] || \
		log_fail "Checksum mismatch for medium_random.$idx: expected=${cksums[$cksum_idx]} got=$newcksum"
	(( cksum_idx = cksum_idx + 1 ))
	(( idx = idx + 1 ))
done

# Verify large random file.
newcksum=$(xxh128digest /$TESTPOOL/large_random.0)
[[ "$newcksum" == "${cksums[$cksum_idx]}" ]] || \
	log_fail "Checksum mismatch for large_random.0: expected=${cksums[$cksum_idx]} got=$newcksum"
(( cksum_idx = cksum_idx + 1 ))

# Verify zero files.
idx=0
while (( idx < 3 )); do
	newcksum=$(xxh128digest /$TESTPOOL/zeros.$idx)
	[[ "$newcksum" == "${cksums[$cksum_idx]}" ]] || \
		log_fail "Checksum mismatch for zeros.$idx: expected=${cksums[$cksum_idx]} got=$newcksum"
	(( cksum_idx = cksum_idx + 1 ))
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

log_pass "anymirror1 pool preserves data correctly across export/import"
