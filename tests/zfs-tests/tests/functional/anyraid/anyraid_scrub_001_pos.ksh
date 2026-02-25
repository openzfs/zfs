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
# Verify that scrub on an AnyRAID pool detects and repairs errors
# from a partially corrupted disk. Create an anymirror1 pool with
# 3 disks, write data, punch a small hole in one disk to cause
# checksum mismatches, run scrub, and verify scrub reports repaired
# blocks and data is still intact.
# This test is self-contained and does not depend on any other test.
#
# STRATEGY:
# 1. Create an anymirror1 pool with 3 disks.
# 2. Write data and record xxh128 checksums.
# 3. Sync and export/import to flush caches.
# 4. Punch a small hole in one disk (enough to cause checksum
#    mismatches but not enough to lose data with parity=1).
# 5. Run scrub.
# 6. Verify scrub reports repaired blocks.
# 7. Verify all data checksums are still correct.
#

verify_runnable "global"

cleanup() {
	poolexists $TESTPOOL && destroy_pool $TESTPOOL
	set_tunable64 ANYRAID_MIN_TILE_SIZE 1073741824
	rm -f $TEST_BASE_DIR/vdev_file.{0,1,2}
}

log_onexit cleanup

log_assert "Scrub on AnyRAID pool detects and repairs errors from corrupted disk"

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
# Write files and record checksums.
#
set -A cksums
typeset -i idx=0

while (( idx < 10 )); do
	log_must file_write -o create -b 1048576 -c 1 -d 'R' \
		-f /$TESTPOOL/file.$idx
	cksums[$idx]=$(xxh128digest /$TESTPOOL/file.$idx)
	(( idx = idx + 1 ))
done

#
# Sync the pool to ensure all data is on disk.
#
log_must zpool sync $TESTPOOL

#
# Export and re-import to flush all caches.
#
log_must zpool export $TESTPOOL
log_must zpool import -d $TEST_BASE_DIR $TESTPOOL

#
# Punch a small hole in one disk to corrupt some blocks.
# This corrupts a region starting at offset 512K with length 64K.
# The mirror parity will allow recovery.
#
log_must punch_hole $((64 * 1024 * 8)) $((64 * 1024)) \
	$TEST_BASE_DIR/vdev_file.1

#
# Run scrub to detect and repair the corruption.
#
log_must zpool scrub $TESTPOOL
log_must zpool wait -t scrub $TESTPOOL

#
# Verify the pool is still ONLINE.
#
log_must check_pool_status $TESTPOOL state ONLINE true

#
# Verify all data checksums are still correct (scrub should have
# repaired the corrupted blocks using parity data).
#
idx=0
typeset -i failedcount=0
while (( idx < 10 )); do
	typeset newcksum=$(xxh128digest /$TESTPOOL/file.$idx)
	if [[ "$newcksum" != "${cksums[$idx]}" ]]; then
		(( failedcount = failedcount + 1 ))
	fi
	(( idx = idx + 1 ))
done

if (( failedcount > 0 )); then
	log_fail "$failedcount of 10 files had wrong checksums after scrub repair"
fi

log_pass "Scrub on AnyRAID pool detects and repairs errors from corrupted disk"
