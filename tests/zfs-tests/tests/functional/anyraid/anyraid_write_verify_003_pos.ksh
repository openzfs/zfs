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
# Write various data patterns to an anyraidz1:2 pool and verify they
# read back correctly after export/import. This tests raidz-style
# AnyRAID with parity=1 and data_width=2 (minimum 3 disks).
#
# STRATEGY:
# 1. Create pool with anyraidz1:2 (4 disks)
# 2. Write files with varied sizes (small 4K, medium 1MiB, large 128MiB)
# 3. Record xxh128 checksums of all files
# 4. Export and re-import the pool
# 5. Verify all checksums match
# 6. Run scrub, verify no errors
#

verify_runnable "global"

cleanup() {
	zpool destroy $TESTPOOL
	set_tunable64 ANYRAID_MIN_TILE_SIZE 1073741824
	rm -f $TEST_BASE_DIR/vdev_file.{0,1,2,3}
}

log_onexit cleanup

log_must truncate -s 1G $TEST_BASE_DIR/vdev_file.{0,1,2,3}

set_tunable64 ANYRAID_MIN_TILE_SIZE 67108864

log_assert "anyraidz1:2 pool preserves data correctly across export/import"

log_must create_pool $TESTPOOL anyraidz1:2 \
	$TEST_BASE_DIR/vdev_file.{0,1,2,3}

#
# Write files of varied sizes and record checksums.
#
set -A cksums
typeset -i cksum_idx=0

#
# Small 4K files (10 files).
#
typeset -i idx=0
while (( idx < 10 )); do
	log_must file_write -o create -b 4096 -c 1 -d 'R' \
		-f /$TESTPOOL/small.$idx
	cksums[$cksum_idx]=$(xxh128digest /$TESTPOOL/small.$idx)
	(( cksum_idx = cksum_idx + 1 ))
	(( idx = idx + 1 ))
done

#
# Medium 1MiB files (5 files).
#
idx=0
while (( idx < 5 )); do
	log_must file_write -o create -b 1048576 -c 1 -d 'R' \
		-f /$TESTPOOL/medium.$idx
	cksums[$cksum_idx]=$(xxh128digest /$TESTPOOL/medium.$idx)
	(( cksum_idx = cksum_idx + 1 ))
	(( idx = idx + 1 ))
done

#
# Large 128MiB file (1 file).
#
log_must file_write -o create -b 1048576 -c 128 -d 'R' \
	-f /$TESTPOOL/large.0
cksums[$cksum_idx]=$(xxh128digest /$TESTPOOL/large.0)
(( cksum_idx = cksum_idx + 1 ))

typeset -i total_files=$cksum_idx

#
# Export and re-import the pool.
#
log_must zpool export $TESTPOOL
log_must zpool import -d $TEST_BASE_DIR $TESTPOOL

#
# Verify all checksums after import.
#
cksum_idx=0

# Verify small files.
idx=0
while (( idx < 10 )); do
	newcksum=$(xxh128digest /$TESTPOOL/small.$idx)
	[[ "$newcksum" == "${cksums[$cksum_idx]}" ]] || \
		log_fail "Checksum mismatch for small.$idx: expected=${cksums[$cksum_idx]} got=$newcksum"
	(( cksum_idx = cksum_idx + 1 ))
	(( idx = idx + 1 ))
done

# Verify medium files.
idx=0
while (( idx < 5 )); do
	newcksum=$(xxh128digest /$TESTPOOL/medium.$idx)
	[[ "$newcksum" == "${cksums[$cksum_idx]}" ]] || \
		log_fail "Checksum mismatch for medium.$idx: expected=${cksums[$cksum_idx]} got=$newcksum"
	(( cksum_idx = cksum_idx + 1 ))
	(( idx = idx + 1 ))
done

# Verify large file.
newcksum=$(xxh128digest /$TESTPOOL/large.0)
[[ "$newcksum" == "${cksums[$cksum_idx]}" ]] || \
	log_fail "Checksum mismatch for large.0: expected=${cksums[$cksum_idx]} got=$newcksum"

#
# Run scrub and verify no errors.
#
log_must zpool scrub $TESTPOOL
log_must zpool wait -t scrub $TESTPOOL

log_must check_pool_status $TESTPOOL state ONLINE true
log_must is_pool_scrubbed $TESTPOOL true

cksum_count=$(zpool status -v $TESTPOOL | grep ONLINE | awk 'NF > 2 && $5 != 0' | wc -l)
[[ "$cksum_count" -eq 0 ]] || log_fail "checksum errors detected after scrub"

log_pass "anyraidz1:2 pool preserves data correctly across export/import"
