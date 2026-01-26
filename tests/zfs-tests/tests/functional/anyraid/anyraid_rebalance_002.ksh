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
# Copyright (c) 2025 Klara, Inc.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Anyraid rebalance works correctly when paused and resumed
#
# STRATEGY:
# 1. Create an anymirror1 vdev with several small disks
# 2. Fill the small disks
# 3. Attach a larger disk
# 4. Rebalance the vdev
# 5. Pause the rebalance
# 6. Export and import the pool
# 7. Resume the rebalance
# 8. Verify that available space has increased after completion
# 9. Verify that scrub found no errors
#

verify_runnable "global"

cleanup() {
	zpool destroy $TESTPOOL
	set_tunable64 ANYRAID_MIN_TILE_SIZE 1073741824
	restore_tunable ANYRAID_RELOCATE_MAX_BYTES_PAUSE
	rm $TEST_BASE_DIR/vdev_file.{0,1,2,3,4,5}
}

log_onexit cleanup

log_must truncate -s 775M $TEST_BASE_DIR/vdev_file.{0,1,2,3,4}
log_must truncate -s 1088M $TEST_BASE_DIR/vdev_file.5
set_tunable64 ANYRAID_MIN_TILE_SIZE 67108864
save_tunable ANYRAID_RELOCATE_MAX_BYTES_PAUSE
set_tunable64 ANYRAID_RELOCATE_MAX_BYTES_PAUSE $((16 * 1024 * 1024))

log_assert "Anyraid rebalance works correctly when paused and resumed"

log_must create_pool $TESTPOOL anymirror1 $TEST_BASE_DIR/vdev_file.{0,1,2,3,4}

log_must file_write -o create -b 1048576 -c 600 -d 'R' -f /$TESTPOOL/f1

cap=$(zpool get -Hp -o value size $TESTPOOL)
[[ "$cap" -eq $((20 * 64 * 1024 * 1024)) ]] || \
	log_fail "Incorrect space for anyraid vdev: $cap"

log_must zpool attach $TESTPOOL anymirror1-0 $TEST_BASE_DIR/vdev_file.5
log_must file_write -o create -b 1048576 -c 240 -d 'R' -f /$TESTPOOL/f2


log_must zpool rebalance $TESTPOOL anymirror1-0
cap=$(zpool get -Hp -o value size $TESTPOOL)
[[ "$cap" -eq $((26 * 64 * 1024 * 1024)) ]] || \
	log_fail "Incorrect space for anyraid vdev: $cap"

log_must sleep 1
log_must zpool export $TESTPOOL
log_must zpool import $TESTPOOL -d $TEST_BASE_DIR

set_tunable64 ANYRAID_RELOCATE_MAX_BYTES_PAUSE 0

log_must zpool wait -t anyraid_relocate,scrub $TESTPOOL
log_must zpool sync $TESTPOOL

cap=$(zpool get -Hp -o value size $TESTPOOL)
[[ "$cap" -eq $((26 * 64 * 1024 * 1024)) ]] || \
	log_fail "Incorrect space for anyraid vdev: $cap"

log_must check_pool_status $TESTPOOL state ONLINE true
log_must is_pool_scrubbed $TESTPOOL true
cksum_count=$(zpool status -v $TESTPOOL | grep ONLINE | awk 'NF > 2 && $5 != 0' | wc -l)
[[ "$cksum_count" -eq 0 ]] || log_fail "checksum errors detected"

log_pass "Anyraid rebalance works correctly"
