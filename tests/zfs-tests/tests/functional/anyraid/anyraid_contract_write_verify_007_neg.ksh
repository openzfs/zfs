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
# Verify that contracting an anymirror1 pool that is nearly full fails
# with ENOSPC because the remaining disks cannot hold all the data.
# After the failed contraction, verify the pool is still healthy and
# all previously written data is intact.
#
# STRATEGY:
# 1. Create anymirror1 pool with 3 small mixed-size disks (512M, 768M, 512M)
#    plus one extra disk (512M) for contraction attempt.
# 2. Set ANYRAID_MIN_TILE_SIZE to 64MiB.
# 3. Write data to fill most of the pool capacity.
# 4. Record xxh128 checksums of written files.
# 5. Attempt to contract by removing one disk -- expect failure (ENOSPC).
# 6. Verify pool is still ONLINE and healthy.
# 7. Verify all file checksums are unchanged.
# 8. Run scrub, verify no corruption.
#

verify_runnable "global"

cleanup() {
	log_note "DEBUG: cleanup started"
	poolexists $TESTPOOL && destroy_pool $TESTPOOL
	set_tunable64 ANYRAID_MIN_TILE_SIZE 1073741824
	rm -f $TEST_BASE_DIR/vdev_file.{0,1,2,3}
}

log_onexit cleanup

log_note "DEBUG: creating small mixed-size sparse files (4 disks)"
log_must truncate -s 512M $TEST_BASE_DIR/vdev_file.0
log_must truncate -s 768M $TEST_BASE_DIR/vdev_file.1
log_must truncate -s 512M $TEST_BASE_DIR/vdev_file.2
log_must truncate -s 512M $TEST_BASE_DIR/vdev_file.3

log_note "DEBUG: setting tile size to 64MiB"
set_tunable64 ANYRAID_MIN_TILE_SIZE 67108864

log_assert "Contracting a nearly-full anymirror1 pool fails with ENOSPC and preserves data"

log_note "DEBUG: creating pool with 4 small disks"
log_must create_pool $TESTPOOL anymirror1 \
	$TEST_BASE_DIR/vdev_file.0 \
	$TEST_BASE_DIR/vdev_file.1 \
	$TEST_BASE_DIR/vdev_file.2 \
	$TEST_BASE_DIR/vdev_file.3

#
# Write data to fill most of the pool. With 4 disks of 512M-768M in
# anymirror1, usable capacity is roughly the sum of unique tile space.
# Write enough to make contraction infeasible. Stop when ENOSPC is hit.
#
log_note "DEBUG: writing files to fill pool"
set -A cksums
typeset -i cksum_idx=0
typeset -i idx=0

while (( idx < 10 )); do
	file_write -o create -b 1048576 -c 40 -d 'R' \
		-f /$TESTPOOL/fill.$idx
	write_rc=$?
	if (( write_rc != 0 )); then
		log_note "DEBUG: fill.$idx write failed (rc=$write_rc), pool is full"
		rm -f /$TESTPOOL/fill.$idx 2>/dev/null
		break
	fi
	cksums[$cksum_idx]=$(xxh128digest /$TESTPOOL/fill.$idx)
	log_note "DEBUG: fill.$idx checksum=${cksums[$cksum_idx]}"
	(( cksum_idx = cksum_idx + 1 ))
	(( idx = idx + 1 ))
done

typeset -i total_files=$cksum_idx
log_note "DEBUG: total files written=$total_files"
(( total_files > 0 )) || log_fail "Could not write any files to pool"

log_note "DEBUG: syncing pool"
log_must zpool sync $TESTPOOL

#
# Attempt to contract the pool by removing disk 3.
# This should fail because the remaining 3 disks cannot hold all data.
#
log_note "DEBUG: attempting contraction (expecting ENOSPC failure)"
zpool contract $TESTPOOL anymirror1-0 \
	$TEST_BASE_DIR/vdev_file.3
contract_rc=$?
log_note "DEBUG: contract returned rc=$contract_rc"

if (( contract_rc == 0 )); then
	log_fail "Contraction succeeded but should have failed with ENOSPC"
fi

#
# Verify the pool is still healthy after the failed contraction.
#
log_note "DEBUG: checking pool is still ONLINE"
log_must check_pool_status $TESTPOOL state ONLINE true

#
# Verify all file checksums are unchanged.
#
log_note "DEBUG: verifying file checksums after failed contraction"
idx=0
while (( idx < total_files )); do
	newcksum=$(xxh128digest /$TESTPOOL/fill.$idx)
	[[ "$newcksum" == "${cksums[$idx]}" ]] || \
		log_fail "Checksum mismatch for fill.$idx: expected=${cksums[$idx]} got=$newcksum"
	(( idx = idx + 1 ))
done

#
# Run scrub to verify no corruption from the failed contraction attempt.
#
log_note "DEBUG: running scrub"
log_must zpool scrub $TESTPOOL
log_must zpool wait -t scrub $TESTPOOL

log_note "DEBUG: checking for checksum errors"
cksum_count=$(zpool status -v $TESTPOOL | grep ONLINE | awk 'NF > 2 && $5 != 0' | wc -l)
(( cksum_count == 0 )) || log_fail "checksum errors detected after scrub"

log_pass "Contracting a nearly-full anymirror1 pool fails with ENOSPC and preserves data"
