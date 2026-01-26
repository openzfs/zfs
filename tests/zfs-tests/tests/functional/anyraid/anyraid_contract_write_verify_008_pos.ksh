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
# Write data to an anymirror1 pool, then launch concurrent writers
# while simultaneously contracting the pool. After contraction and
# all writers complete, verify all data reads back correctly.
# This exercises AnyRAID under concurrent I/O pressure during
# contraction.
#
# STRATEGY:
# 1. Create anymirror1 pool with 5 disks.
# 2. Set ANYRAID_MIN_TILE_SIZE to 64MiB.
# 3. Write initial data files and record checksums.
# 4. Start contraction to remove one disk.
# 5. Launch 8 background file_write processes writing to separate files.
# 6. Wait for contraction and all writers to complete.
# 7. Record checksums of concurrent-written files.
# 8. Export and re-import the pool.
# 9. Verify all checksums match (both initial and concurrent files).
# 10. Run scrub, verify no errors.
#

verify_runnable "global"

NUM_WRITERS=8
FILE_SIZE_MB=16

cleanup() {
	log_note "DEBUG: cleanup started"
	poolexists $TESTPOOL && destroy_pool $TESTPOOL
	set_tunable64 ANYRAID_MIN_TILE_SIZE 1073741824
	rm -f $TEST_BASE_DIR/vdev_file.{0,1,2,3,4}
}

log_onexit cleanup

log_note "DEBUG: creating sparse files (5 disks)"
log_must truncate -s 1G $TEST_BASE_DIR/vdev_file.{0,1,2,3,4}

log_note "DEBUG: setting tile size to 64MiB"
set_tunable64 ANYRAID_MIN_TILE_SIZE 67108864

log_assert "Concurrent writes during anymirror1 contraction preserve data correctly"

log_note "DEBUG: creating pool with 5 disks"
log_must create_pool $TESTPOOL anymirror1 \
	$TEST_BASE_DIR/vdev_file.{0,1,2,3,4}

#
# Write initial data files and record checksums.
#
log_note "DEBUG: writing initial data files"
set -A init_cksums
typeset -i idx=0
while (( idx < 5 )); do
	log_must file_write -o create -b 1048576 -c 10 -d 'R' \
		-f /$TESTPOOL/initial.$idx
	init_cksums[$idx]=$(xxh128digest /$TESTPOOL/initial.$idx)
	log_note "DEBUG: initial.$idx checksum=${init_cksums[$idx]}"
	(( idx = idx + 1 ))
done

log_note "DEBUG: syncing pool before contraction"
log_must zpool sync $TESTPOOL

#
# Start contraction to remove disk 4.
#
log_note "DEBUG: starting contraction to remove vdev_file.4"
log_must zpool contract $TESTPOOL anymirror1-0 \
	$TEST_BASE_DIR/vdev_file.4

#
# Launch concurrent writers while contraction is in progress.
# Each writes a 16MiB file with random data.
#
log_note "DEBUG: launching $NUM_WRITERS concurrent writers"
idx=0
while (( idx < NUM_WRITERS )); do
	file_write -o create -b 1048576 -c $FILE_SIZE_MB -d 'R' \
		-f /$TESTPOOL/concurrent.$idx &
	(( idx = idx + 1 ))
done

log_note "DEBUG: waiting for contraction to complete"
log_must zpool wait -t anyraid_relocate $TESTPOOL

log_note "DEBUG: waiting for all background writers to complete"
wait

log_must zpool sync $TESTPOOL

#
# Verify all concurrent files exist and record checksums.
#
log_note "DEBUG: recording checksums of concurrent files"
set -A conc_cksums
idx=0
while (( idx < NUM_WRITERS )); do
	if [[ ! -f /$TESTPOOL/concurrent.$idx ]]; then
		log_fail "File concurrent.$idx was not created by background writer"
	fi
	conc_cksums[$idx]=$(xxh128digest /$TESTPOOL/concurrent.$idx)
	log_note "DEBUG: concurrent.$idx checksum=${conc_cksums[$idx]}"
	(( idx = idx + 1 ))
done

#
# Remove the detached vdev file to avoid "more than one matching pool"
# on import (the detached file still contains stale pool metadata).
#
log_note "DEBUG: removing detached vdev file"
rm -f $TEST_BASE_DIR/vdev_file.4

#
# Export and re-import the pool.
#
log_note "DEBUG: exporting pool"
log_must zpool export $TESTPOOL
log_note "DEBUG: importing pool"
log_must zpool import -d $TEST_BASE_DIR $TESTPOOL

#
# Verify all initial file checksums after contraction and import.
#
log_note "DEBUG: verifying initial file checksums"
idx=0
while (( idx < 5 )); do
	newcksum=$(xxh128digest /$TESTPOOL/initial.$idx)
	[[ "$newcksum" == "${init_cksums[$idx]}" ]] || \
		log_fail "Checksum mismatch for initial.$idx: expected=${init_cksums[$idx]} got=$newcksum"
	(( idx = idx + 1 ))
done

#
# Verify all concurrent file checksums after import.
#
log_note "DEBUG: verifying concurrent file checksums"
idx=0
while (( idx < NUM_WRITERS )); do
	newcksum=$(xxh128digest /$TESTPOOL/concurrent.$idx)
	[[ "$newcksum" == "${conc_cksums[$idx]}" ]] || \
		log_fail "Checksum mismatch for concurrent.$idx: expected=${conc_cksums[$idx]} got=$newcksum"
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

log_pass "Concurrent writes during anymirror1 contraction preserve data correctly"
