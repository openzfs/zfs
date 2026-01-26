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
# Write a large sequential file to an anymirror1 pool with mixed-size
# disks, contract the pool by removing one disk, then verify data
# integrity across export/import. This exercises AnyRAID's tile layout
# with heterogeneous disk sizes under contraction.
#
# STRATEGY:
# 1. Create anymirror1 pool with 4 mixed-size disks (1G, 1536M, 2G, 1G).
# 2. Set ANYRAID_MIN_TILE_SIZE to 64MiB.
# 3. Write a large sequential file (~400MiB).
# 4. Record xxh128 checksum.
# 5. Contract the pool by removing disk 3 (the extra 1G disk).
# 6. Wait for relocation to complete.
# 7. Export and re-import the pool.
# 8. Verify checksum matches.
# 9. Run scrub, verify no errors.
#

verify_runnable "global"

cleanup() {
	log_note "DEBUG: cleanup started"
	poolexists $TESTPOOL && destroy_pool $TESTPOOL
	set_tunable64 ANYRAID_MIN_TILE_SIZE 1073741824
	rm -f $TEST_BASE_DIR/vdev_file.{0,1,2,3}
}

log_onexit cleanup

log_note "DEBUG: creating mixed-size sparse files (4 disks)"
log_must truncate -s 1G $TEST_BASE_DIR/vdev_file.0
log_must truncate -s 1536M $TEST_BASE_DIR/vdev_file.1
log_must truncate -s 2G $TEST_BASE_DIR/vdev_file.2
log_must truncate -s 1G $TEST_BASE_DIR/vdev_file.3

log_note "DEBUG: setting tile size to 64MiB"
set_tunable64 ANYRAID_MIN_TILE_SIZE 67108864

log_assert "Large sequential write to anymirror1 with mixed-size disks preserves data after contraction"

log_note "DEBUG: creating pool with 4 mixed-size disks"
log_must create_pool $TESTPOOL anymirror1 \
	$TEST_BASE_DIR/vdev_file.0 \
	$TEST_BASE_DIR/vdev_file.1 \
	$TEST_BASE_DIR/vdev_file.2 \
	$TEST_BASE_DIR/vdev_file.3

#
# Write a large sequential file (~400MiB) to fill a portion of the pool.
# Using file_write with 1MiB blocks x 400 count.
#
log_note "DEBUG: writing 400MiB sequential file"
log_must file_write -o create -b 1048576 -c 400 -d 'R' \
	-f /$TESTPOOL/large_sequential.0

log_note "DEBUG: recording checksum"
cksum_large=$(xxh128digest /$TESTPOOL/large_sequential.0)
log_note "DEBUG: large_sequential.0 checksum=$cksum_large"

#
# Contract the pool by removing disk 3.
#
log_note "DEBUG: syncing pool before contraction"
log_must zpool sync $TESTPOOL

log_note "DEBUG: starting contraction to remove vdev_file.3"
log_must zpool contract $TESTPOOL anymirror1-0 \
	$TEST_BASE_DIR/vdev_file.3

log_note "DEBUG: waiting for relocation to complete"
log_must zpool wait -t anyraid_relocate $TESTPOOL
log_must zpool sync $TESTPOOL

#
# Remove the detached vdev file to avoid "more than one matching pool"
# on import (the detached file still contains stale pool metadata).
#
log_note "DEBUG: removing detached vdev file"
rm -f $TEST_BASE_DIR/vdev_file.3

#
# Export and re-import the pool.
#
log_note "DEBUG: exporting pool"
log_must zpool export $TESTPOOL
log_note "DEBUG: importing pool"
log_must zpool import -d $TEST_BASE_DIR $TESTPOOL

#
# Verify checksum after contraction and import.
#
log_note "DEBUG: verifying checksum after contraction"
newcksum=$(xxh128digest /$TESTPOOL/large_sequential.0)
log_note "DEBUG: new checksum=$newcksum"
[[ "$newcksum" == "$cksum_large" ]] || \
	log_fail "Checksum mismatch for large_sequential.0: expected=$cksum_large got=$newcksum"

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

log_pass "Large sequential write to anymirror1 with mixed-size disks preserves data after contraction"
