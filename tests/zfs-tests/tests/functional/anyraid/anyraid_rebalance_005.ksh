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
# Anyraid rebalance with a concurrent scrub does not cause errors
# or data corruption. The rebalance operation automatically triggers
# a scrub upon completion. This test fills the pool with enough data
# to make the rebalance take significant time, then issues an explicit
# scrub immediately after starting the rebalance. If the explicit scrub
# fails because a scrub is already in progress (auto-started by the
# rebalance), that is acceptable. The test verifies data integrity and
# pool health after both operations complete.
#
# STRATEGY:
# 1. Create an anymirror1 vdev with several small disks
# 2. Fill with substantial data and record checksums
# 3. Attach a larger disk
# 4. Start rebalance
# 5. Immediately attempt to start a scrub
# 6. Wait for rebalance and scrub to complete
# 7. Verify all data checksums are unchanged
# 8. Verify pool health and no checksum errors
#

verify_runnable "global"

cleanup() {
	zpool destroy $TESTPOOL
	set_tunable64 ANYRAID_MIN_TILE_SIZE 1073741824
	rm $TEST_BASE_DIR/vdev_file.{0,1,2,3,4,5}
}

log_onexit cleanup

log_must truncate -s 768M $TEST_BASE_DIR/vdev_file.{0,1,2,3,4}
log_must truncate -s 10G $TEST_BASE_DIR/vdev_file.5

set_tunable64 ANYRAID_MIN_TILE_SIZE 67108864

log_assert "Scrub during anyraid rebalance does not cause errors or corruption"

log_must create_pool $TESTPOOL anymirror1 $TEST_BASE_DIR/vdev_file.{0,1,2,3,4}

#
# Write substantial data and record checksums. Use enough data so that
# the rebalance takes measurable time.
#
typeset -i file_count=10
typeset -i idx=0
set -A cksums

while (( idx < file_count )); do
	log_must file_write -o create -b 1048576 -c 50 -d 'R' \
		-f /$TESTPOOL/file.$idx
	cksums[$idx]=$(xxh128digest /$TESTPOOL/file.$idx)
	(( idx = idx + 1 ))
done

#
# Attach a larger disk and start rebalance.
#
log_must zpool attach $TESTPOOL anymirror1-0 $TEST_BASE_DIR/vdev_file.5
log_must zpool rebalance $TESTPOOL anymirror1-0

#
# Immediately attempt a scrub. The rebalance may auto-start a scrub,
# so this may fail with "currently scrubbing" which is acceptable.
# Either way, a scrub will run concurrently with the rebalance.
#
zpool scrub $TESTPOOL
if [[ $? -ne 0 ]]; then
	log_note "Scrub already in progress (auto-started by rebalance), continuing"
fi

log_must zpool wait -t anyraid_relocate,scrub $TESTPOOL
log_must zpool sync $TESTPOOL

#
# Verify all file checksums are unchanged.
#
idx=0
while (( idx < file_count )); do
	newcksum=$(xxh128digest /$TESTPOOL/file.$idx)
	[[ "$newcksum" == "${cksums[$idx]}" ]] || \
		log_fail "Checksum mismatch for file.$idx: expected=${cksums[$idx]} got=$newcksum"
	(( idx = idx + 1 ))
done

#
# Verify pool health and no checksum errors.
#
log_must check_pool_status $TESTPOOL state ONLINE true
log_must is_pool_scrubbed $TESTPOOL true
cksum_count=$(zpool status -v $TESTPOOL | grep ONLINE | awk 'NF > 2 && $5 != 0' | wc -l)
[[ "$cksum_count" -eq 0 ]] || log_fail "checksum errors detected"

log_pass "Scrub during anyraid rebalance does not cause errors or corruption"
