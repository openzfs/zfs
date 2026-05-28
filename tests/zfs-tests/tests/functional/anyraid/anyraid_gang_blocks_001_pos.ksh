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

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Verify AnyRAID handles gang blocks correctly when the pool is
# nearly full. Fill an anymirror1 pool with small disks to near
# capacity, then continue writing to force gang block allocation.
# Verify data integrity after the writes complete.
# This test is self-contained and does not depend on any other test.
#
# STRATEGY:
# 1. Create an anymirror1 pool with small disks (512MiB each).
# 2. Fill the pool to near capacity with known data.
# 3. Continue writing small files to push into gang block territory.
# 4. Record checksums of all successfully written files.
# 5. Export and re-import the pool.
# 6. Verify all checksums match.
# 7. Run scrub, verify no errors.
#

verify_runnable "global"

cleanup() {
	poolexists $TESTPOOL && destroy_pool $TESTPOOL
	set_tunable64 ANYRAID_MIN_TILE_SIZE 1073741824
	rm -f $TEST_BASE_DIR/vdev_file.{0,1,2}
}

log_onexit cleanup

log_assert "AnyRAID handles gang blocks correctly when pool is nearly full"

#
# Create small backing files and set tile size.
#
log_must truncate -s 512M $TEST_BASE_DIR/vdev_file.{0,1,2}
set_tunable64 ANYRAID_MIN_TILE_SIZE 67108864

#
# Create the pool.
#
log_must create_pool $TESTPOOL anymirror1 \
	$TEST_BASE_DIR/vdev_file.{0,1,2}

#
# Fill the pool with 1MiB files until we approach capacity.
# With anymirror1 (parity=1) and 3 x 512MiB disks, usable
# capacity is roughly 768MiB (1536MiB / 2). Write ~700MiB
# to get close to full.
#
set -A cksums
typeset -i file_idx=0
typeset -i fill_count=700

while (( file_idx < fill_count )); do
	file_write -o create -b 1048576 -c 1 -d 'R' \
		-f /$TESTPOOL/fill.$file_idx 2>/dev/null
	if (( $? != 0 )); then
		break
	fi
	cksums[$file_idx]=$(xxh128digest /$TESTPOOL/fill.$file_idx)
	(( file_idx = file_idx + 1 ))
done

typeset -i total_files=$file_idx

#
# Now write small 4K files to push further into fragmented/gang
# block territory. These writes may fail with ENOSPC which is
# expected.
#
typeset -i small_idx=0
typeset -i small_count=0

while (( small_idx < 100 )); do
	dd if=/dev/urandom of=/$TESTPOOL/small.$small_idx \
		bs=4096 count=1 2>/dev/null
	if (( $? != 0 )); then
		break
	fi
	cksums[$((total_files + small_idx))]=$(xxh128digest /$TESTPOOL/small.$small_idx)
	(( small_idx = small_idx + 1 ))
done

small_count=$small_idx
typeset -i all_files=$((total_files + small_count))

#
# Sync and verify pool is still ONLINE.
#
log_must zpool sync $TESTPOOL
log_must check_pool_status $TESTPOOL state ONLINE true

#
# Export and re-import the pool.
#
log_must zpool export $TESTPOOL
log_must zpool import -d $TEST_BASE_DIR $TESTPOOL

#
# Verify all fill file checksums.
#
typeset -i failedcount=0
file_idx=0
while (( file_idx < total_files )); do
	typeset newcksum=$(xxh128digest /$TESTPOOL/fill.$file_idx)
	if [[ "$newcksum" != "${cksums[$file_idx]}" ]]; then
		(( failedcount = failedcount + 1 ))
	fi
	(( file_idx = file_idx + 1 ))
done

#
# Verify all small file checksums.
#
small_idx=0
while (( small_idx < small_count )); do
	typeset newcksum=$(xxh128digest /$TESTPOOL/small.$small_idx)
	typeset expected_idx=$((total_files + small_idx))
	if [[ "$newcksum" != "${cksums[$expected_idx]}" ]]; then
		(( failedcount = failedcount + 1 ))
	fi
	(( small_idx = small_idx + 1 ))
done

if (( failedcount > 0 )); then
	log_fail "$failedcount of $all_files files had wrong checksums after export/import"
fi

#
# Run scrub and verify no errors.
#
log_must zpool scrub $TESTPOOL
log_must zpool wait -t scrub $TESTPOOL

log_must check_pool_status $TESTPOOL state ONLINE true

log_pass "AnyRAID handles gang blocks correctly when pool is nearly full"
