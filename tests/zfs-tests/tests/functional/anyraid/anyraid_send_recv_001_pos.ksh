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
# Verify ZFS send/receive works with AnyRAID pool as source and
# destination. Create an anymirror1 source pool, write data, take
# a snapshot, send the snapshot to a second AnyRAID pool, and
# verify the received data matches the source checksums.
# This test is self-contained and does not depend on any other test.
#
# STRATEGY:
# 1. Create an anymirror1 source pool with 3 disks.
# 2. Write data and record xxh128 checksums.
# 3. Take a snapshot.
# 4. Create a second anymirror1 destination pool with 3 disks.
# 5. Send the snapshot to the destination pool via zfs send | zfs recv.
# 6. Verify received data matches the source checksums.
# 7. Run scrub on both pools, verify no errors.
#

verify_runnable "global"

SRC_POOL="${TESTPOOL}_src"
DST_POOL="${TESTPOOL}_dst"
SNAP_NAME="$SRC_POOL@send_snap"

cleanup() {
	poolexists $SRC_POOL && destroy_pool $SRC_POOL
	poolexists $DST_POOL && destroy_pool $DST_POOL
	set_tunable64 ANYRAID_MIN_TILE_SIZE 1073741824
	rm -f $TEST_BASE_DIR/src_vdev.{0,1,2}
	rm -f $TEST_BASE_DIR/dst_vdev.{0,1,2}
}

log_onexit cleanup

log_assert "ZFS send/receive works correctly with AnyRAID pools"

#
# Create backing files and set tile size.
#
log_must truncate -s 1G $TEST_BASE_DIR/src_vdev.{0,1,2}
log_must truncate -s 1G $TEST_BASE_DIR/dst_vdev.{0,1,2}
set_tunable64 ANYRAID_MIN_TILE_SIZE 67108864

#
# Create the source pool.
#
log_must zpool create -f $SRC_POOL anymirror1 \
	$TEST_BASE_DIR/src_vdev.{0,1,2}

#
# Write files and record checksums on the source pool.
#
set -A cksums
typeset -i idx=0

while (( idx < 8 )); do
	log_must file_write -o create -b 1048576 -c 1 -d 'R' \
		-f /$SRC_POOL/file.$idx
	cksums[$idx]=$(xxh128digest /$SRC_POOL/file.$idx)
	(( idx = idx + 1 ))
done

#
# Also write a larger file.
#
log_must file_write -o create -b 1048576 -c 16 -d 'R' \
	-f /$SRC_POOL/largefile
typeset large_cksum=$(xxh128digest /$SRC_POOL/largefile)

log_must zpool sync $SRC_POOL

#
# Take a snapshot on the source pool.
#
log_must zfs snapshot $SNAP_NAME

#
# Create the destination pool.
#
log_must zpool create -f $DST_POOL anymirror1 \
	$TEST_BASE_DIR/dst_vdev.{0,1,2}

#
# Send the snapshot to the destination pool.
#
log_must eval "zfs send $SNAP_NAME | zfs recv $DST_POOL/received"

#
# Verify received data checksums match the source.
#
idx=0
while (( idx < 8 )); do
	typeset newcksum=$(xxh128digest /$DST_POOL/received/file.$idx)
	[[ "$newcksum" == "${cksums[$idx]}" ]] || \
		log_fail "Checksum mismatch for received file.$idx: expected=${cksums[$idx]} got=$newcksum"
	(( idx = idx + 1 ))
done

typeset new_large_cksum=$(xxh128digest /$DST_POOL/received/largefile)
[[ "$new_large_cksum" == "$large_cksum" ]] || \
	log_fail "Checksum mismatch for received largefile: expected=$large_cksum got=$new_large_cksum"

#
# Run scrub on both pools and verify no errors.
#
log_must zpool scrub $SRC_POOL
log_must zpool wait -t scrub $SRC_POOL
log_must check_pool_status $SRC_POOL state ONLINE true
log_must is_pool_scrubbed $SRC_POOL true

typeset src_cksum_count=$(zpool status -v $SRC_POOL | grep ONLINE | \
	awk 'NF > 2 && $5 != 0' | wc -l)
(( src_cksum_count == 0 )) || \
	log_fail "Checksum errors detected on source pool after scrub"

log_must zpool scrub $DST_POOL
log_must zpool wait -t scrub $DST_POOL
log_must check_pool_status $DST_POOL state ONLINE true
log_must is_pool_scrubbed $DST_POOL true

typeset dst_cksum_count=$(zpool status -v $DST_POOL | grep ONLINE | \
	awk 'NF > 2 && $5 != 0' | wc -l)
(( dst_cksum_count == 0 )) || \
	log_fail "Checksum errors detected on destination pool after scrub"

log_pass "ZFS send/receive works correctly with AnyRAID pools"
