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
# Copyright (c) 2025, Klara, Inc.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Verify snapshots and rollbacks work correctly on AnyRAID pools.
# Write file A, take a snapshot, write file B, rollback to the
# snapshot, and verify file A exists while file B does not.
# This test is self-contained and does not depend on any other test.
#
# STRATEGY:
# 1. Create an anymirror1 pool with 3 disks.
# 2. Write file A and record its checksum.
# 3. Take a snapshot.
# 4. Write file B.
# 5. Rollback to the snapshot.
# 6. Verify file A exists with correct checksum.
# 7. Verify file B does not exist.
# 8. Export/import to verify persistence.
# 9. Run scrub, verify no errors.
#

verify_runnable "global"

SNAP_NAME="$TESTPOOL@snap1"

cleanup() {
	poolexists $TESTPOOL && destroy_pool $TESTPOOL
	set_tunable64 ANYRAID_MIN_TILE_SIZE 1073741824
	rm -f $TEST_BASE_DIR/vdev_file.{0,1,2}
}

log_onexit cleanup

log_assert "AnyRAID snapshots and rollbacks preserve data correctly"

#
# Create backing files and set tile size.
#
log_must truncate -s 1G $TEST_BASE_DIR/vdev_file.{0,1,2}
set_tunable64 ANYRAID_MIN_TILE_SIZE 67108864

#
# Create the pool.
#
log_must create_pool $TESTPOOL anymirror1 \
	$TEST_BASE_DIR/vdev_file.{0,1,2}

#
# Write file A and record its checksum.
#
log_must file_write -o create -b 1048576 -c 4 -d 'R' \
	-f /$TESTPOOL/file_a
typeset cksum_a=$(xxh128digest /$TESTPOOL/file_a)

log_must zpool sync $TESTPOOL

#
# Take a snapshot.
#
log_must zfs snapshot $SNAP_NAME

#
# Write file B after the snapshot.
#
log_must file_write -o create -b 1048576 -c 4 -d 'R' \
	-f /$TESTPOOL/file_b

[[ -f /$TESTPOOL/file_b ]] || \
	log_fail "file_b should exist before rollback"

log_must zpool sync $TESTPOOL

#
# Rollback to the snapshot.
#
log_must zfs rollback $SNAP_NAME

#
# Verify file A still exists with the correct checksum.
#
[[ -f /$TESTPOOL/file_a ]] || \
	log_fail "file_a should exist after rollback"

typeset new_cksum_a=$(xxh128digest /$TESTPOOL/file_a)
[[ "$new_cksum_a" == "$cksum_a" ]] || \
	log_fail "file_a checksum mismatch after rollback: expected=$cksum_a got=$new_cksum_a"

#
# Verify file B does NOT exist after rollback.
#
[[ ! -f /$TESTPOOL/file_b ]] || \
	log_fail "file_b should not exist after rollback to snapshot"

#
# Export/import to verify persistence of the rollback state.
#
log_must zpool export $TESTPOOL
log_must zpool import -d $TEST_BASE_DIR $TESTPOOL

[[ -f /$TESTPOOL/file_a ]] || \
	log_fail "file_a should exist after export/import"

typeset reimport_cksum_a=$(xxh128digest /$TESTPOOL/file_a)
[[ "$reimport_cksum_a" == "$cksum_a" ]] || \
	log_fail "file_a checksum mismatch after reimport: expected=$cksum_a got=$reimport_cksum_a"

[[ ! -f /$TESTPOOL/file_b ]] || \
	log_fail "file_b should not exist after export/import post-rollback"

#
# Run scrub and verify no errors.
#
log_must zpool scrub $TESTPOOL
log_must zpool wait -t scrub $TESTPOOL

log_must check_pool_status $TESTPOOL state ONLINE true
log_must is_pool_scrubbed $TESTPOOL true

typeset cksum_count=$(zpool status -v $TESTPOOL | grep ONLINE | \
	awk 'NF > 2 && $5 != 0' | wc -l)
(( cksum_count == 0 )) || log_fail "Checksum errors detected after scrub"

log_pass "AnyRAID snapshots and rollbacks preserve data correctly"
