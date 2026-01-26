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
# Write various data patterns to an anyraidz2:2 AnyRAID pool, contract
# the pool by removing one disk, then verify all data reads back
# correctly. This tests raidz2 with data width of 2.
#
# STRATEGY:
# 1. Create pool with anyraidz2:2 (6 disks: 2 parity + 2 data = 4 min, use 6)
# 2. Write multiple files with different patterns and varied sizes
# 3. Record xxh128 checksums of all files
# 4. Contract the pool by removing one disk
# 5. Wait for relocation to complete
# 6. Export and re-import the pool
# 7. Verify all checksums match
# 8. Run scrub, verify no errors
#

verify_runnable "global"

cleanup() {
	log_note "DEBUG: cleanup started"
	poolexists $TESTPOOL && destroy_pool $TESTPOOL
	set_tunable64 ANYRAID_MIN_TILE_SIZE 1073741824
	rm -f $TEST_BASE_DIR/vdev_file.{0,1,2,3,4,5}
}

log_onexit cleanup

log_note "DEBUG: creating sparse files (6 disks)"
log_must truncate -s 1G $TEST_BASE_DIR/vdev_file.{0,1,2,3,4,5}

log_note "DEBUG: setting tile size to 64MiB"
set_tunable64 ANYRAID_MIN_TILE_SIZE 67108864

log_assert "anyraidz2:2 contraction preserves data correctly across export/import"

log_note "DEBUG: creating anyraidz2:2 pool with 6 disks"
log_must create_pool $TESTPOOL anyraidz2:2 \
	$TEST_BASE_DIR/vdev_file.{0,1,2,3,4,5}

#
# Write files of varied sizes and patterns, record checksums.
#
log_note "DEBUG: writing test files and recording checksums"
set -A cksums
typeset -i cksum_idx=0

#
# Small 4K files with random data (10 files).
#
log_note "DEBUG: writing 10 small 4K random files"
typeset -i idx=0
while (( idx < 10 )); do
	log_must file_write -o create -b 4096 -c 1 -d 'R' \
		-f /$TESTPOOL/small_random.$idx
	cksums[$cksum_idx]=$(xxh128digest /$TESTPOOL/small_random.$idx)
	log_note "DEBUG: small_random.$idx checksum=${cksums[$cksum_idx]}"
	(( cksum_idx = cksum_idx + 1 ))
	(( idx = idx + 1 ))
done

#
# Medium 1MiB files with random data (5 files).
#
log_note "DEBUG: writing 5 medium 1MiB random files"
idx=0
while (( idx < 5 )); do
	log_must file_write -o create -b 1048576 -c 1 -d 'R' \
		-f /$TESTPOOL/medium_random.$idx
	cksums[$cksum_idx]=$(xxh128digest /$TESTPOOL/medium_random.$idx)
	log_note "DEBUG: medium_random.$idx checksum=${cksums[$cksum_idx]}"
	(( cksum_idx = cksum_idx + 1 ))
	(( idx = idx + 1 ))
done

#
# Large 128MiB file with random data (1 file).
#
log_note "DEBUG: writing 1 large 128MiB random file"
log_must file_write -o create -b 1048576 -c 128 -d 'R' \
	-f /$TESTPOOL/large_random.0
cksums[$cksum_idx]=$(xxh128digest /$TESTPOOL/large_random.0)
log_note "DEBUG: large_random.0 checksum=${cksums[$cksum_idx]}"
(( cksum_idx = cksum_idx + 1 ))

#
# Zero-filled files (3 files at 64K each).
#
log_note "DEBUG: writing 3 zero-filled 64K files"
idx=0
while (( idx < 3 )); do
	log_must dd if=/dev/zero of=/$TESTPOOL/zeros.$idx \
		bs=65536 count=1 2>/dev/null
	cksums[$cksum_idx]=$(xxh128digest /$TESTPOOL/zeros.$idx)
	log_note "DEBUG: zeros.$idx checksum=${cksums[$cksum_idx]}"
	(( cksum_idx = cksum_idx + 1 ))
	(( idx = idx + 1 ))
done

typeset -i total_files=$cksum_idx
log_note "DEBUG: total files written=$total_files"

#
# Contract the pool by removing disk 5.
#
log_note "DEBUG: syncing pool before contraction"
log_must zpool sync $TESTPOOL

log_note "DEBUG: looking up anyraidz vdev name"
vdev_name=$(zpool status $TESTPOOL | awk '/anyraidz/ && /raidz/ {print $1; exit}')
log_note "DEBUG: vdev name=$vdev_name"
[[ -n "$vdev_name" ]] || log_fail "Could not find anyraidz vdev name"

log_note "DEBUG: starting contraction to remove vdev_file.5"
log_must zpool contract $TESTPOOL $vdev_name \
	$TEST_BASE_DIR/vdev_file.5

log_note "DEBUG: waiting for relocation to complete"
log_must zpool wait -t anyraid_relocate $TESTPOOL
log_must zpool sync $TESTPOOL

#
# Remove the detached vdev file to avoid "more than one matching pool"
# on import (the detached file still contains stale pool metadata).
#
log_note "DEBUG: removing detached vdev file"
rm -f $TEST_BASE_DIR/vdev_file.5

#
# Export and re-import the pool.
#
log_note "DEBUG: exporting pool"
log_must zpool export $TESTPOOL
log_note "DEBUG: importing pool"
log_must zpool import -d $TEST_BASE_DIR $TESTPOOL

#
# Verify all checksums after contraction and import.
#
log_note "DEBUG: verifying checksums after contraction"
cksum_idx=0

# Verify small random files.
log_note "DEBUG: verifying small random files"
idx=0
while (( idx < 10 )); do
	newcksum=$(xxh128digest /$TESTPOOL/small_random.$idx)
	[[ "$newcksum" == "${cksums[$cksum_idx]}" ]] || \
		log_fail "Checksum mismatch for small_random.$idx: expected=${cksums[$cksum_idx]} got=$newcksum"
	(( cksum_idx = cksum_idx + 1 ))
	(( idx = idx + 1 ))
done

# Verify medium random files.
log_note "DEBUG: verifying medium random files"
idx=0
while (( idx < 5 )); do
	newcksum=$(xxh128digest /$TESTPOOL/medium_random.$idx)
	[[ "$newcksum" == "${cksums[$cksum_idx]}" ]] || \
		log_fail "Checksum mismatch for medium_random.$idx: expected=${cksums[$cksum_idx]} got=$newcksum"
	(( cksum_idx = cksum_idx + 1 ))
	(( idx = idx + 1 ))
done

# Verify large random file.
log_note "DEBUG: verifying large random file"
newcksum=$(xxh128digest /$TESTPOOL/large_random.0)
[[ "$newcksum" == "${cksums[$cksum_idx]}" ]] || \
	log_fail "Checksum mismatch for large_random.0: expected=${cksums[$cksum_idx]} got=$newcksum"
(( cksum_idx = cksum_idx + 1 ))

# Verify zero files.
log_note "DEBUG: verifying zero-filled files"
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
log_note "DEBUG: running scrub"
log_must zpool scrub $TESTPOOL
log_must zpool wait -t scrub $TESTPOOL

log_note "DEBUG: checking pool status"
log_must check_pool_status $TESTPOOL state ONLINE true
log_must is_pool_scrubbed $TESTPOOL true

cksum_count=$(zpool status -v $TESTPOOL | grep ONLINE | awk 'NF > 2 && $5 != 0' | wc -l)
(( cksum_count == 0 )) || log_fail "checksum errors detected after scrub"

log_pass "anyraidz2:2 contraction preserves data correctly across export/import"
