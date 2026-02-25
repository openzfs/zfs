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
# Write a large sequential file that fills a significant portion of an
# anymirror1 pool with mixed-size disks, then verify data integrity
# across export/import. This exercises AnyRAID's tile layout with
# heterogeneous disk sizes under heavy sequential write load.
#
# STRATEGY:
# 1. Create anymirror1 pool with 3 mixed-size disks (1G, 1536M, 2G).
# 2. Set ANYRAID_MIN_TILE_SIZE to 64MiB.
# 3. Write a large sequential file (~600MiB) using most pool capacity.
# 4. Record xxh128 checksum.
# 5. Export and re-import the pool.
# 6. Verify checksum matches.
# 7. Run scrub, verify no errors.
#

verify_runnable "global"

cleanup() {
	zpool destroy $TESTPOOL
	set_tunable64 ANYRAID_MIN_TILE_SIZE 1073741824
	rm -f $TEST_BASE_DIR/vdev_file.{0,1,2}
}

log_onexit cleanup

log_must truncate -s 1G $TEST_BASE_DIR/vdev_file.0
log_must truncate -s 1536M $TEST_BASE_DIR/vdev_file.1
log_must truncate -s 2G $TEST_BASE_DIR/vdev_file.2

set_tunable64 ANYRAID_MIN_TILE_SIZE 67108864

log_assert "Large sequential write to anymirror1 pool with mixed-size disks preserves data across export/import"

log_must create_pool $TESTPOOL anymirror1 \
	$TEST_BASE_DIR/vdev_file.0 \
	$TEST_BASE_DIR/vdev_file.1 \
	$TEST_BASE_DIR/vdev_file.2

#
# Write a large sequential file (~600MiB) to fill most of the pool.
# Using file_write with 1MiB blocks x 600 count.
#
log_must file_write -o create -b 1048576 -c 600 -d 'R' \
	-f /$TESTPOOL/large_sequential.0

cksum_large=$(xxh128digest /$TESTPOOL/large_sequential.0)

#
# Export and re-import the pool.
#
log_must zpool sync $TESTPOOL
log_must zpool export $TESTPOOL
log_must zpool import -d $TEST_BASE_DIR $TESTPOOL

#
# Verify checksum after import.
#
newcksum=$(xxh128digest /$TESTPOOL/large_sequential.0)
[[ "$newcksum" == "$cksum_large" ]] || \
	log_fail "Checksum mismatch for large_sequential.0: expected=$cksum_large got=$newcksum"

#
# Run scrub and verify no errors.
#
log_must zpool scrub $TESTPOOL
log_must zpool wait -t scrub $TESTPOOL

log_must check_pool_status $TESTPOOL state ONLINE true
log_must is_pool_scrubbed $TESTPOOL true

cksum_count=$(zpool status -v $TESTPOOL | grep ONLINE | awk 'NF > 2 && $5 != 0' | wc -l)
(( cksum_count == 0 )) || log_fail "checksum errors detected after scrub"

log_pass "Large sequential write to anymirror1 pool with mixed-size disks preserves data across export/import"
