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
# Anyraid rebalance works correctly
#
# STRATEGY:
# 1. Create an anymirror1 vdev with several small disks
# 2. Fill the small disks
# 3. Attach a larger disk
# 4. Rebalance the vdev
# 5. Verify that available space has increased after completion
# 6. Verify that scrub found no errors
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

log_assert "Anyraid rebalance works correctly"

log_must create_pool $TESTPOOL anymirror1 $TEST_BASE_DIR/vdev_file.{0,1,2,3,4}

log_must file_write -o create -b 1048576 -c 950 -d 'R' -f /$TESTPOOL/f1

cap=$(zpool get -Hp -o value size $TESTPOOL)
[[ "$cap" -eq $((17 * 64 * 1024 * 1024)) ]] || \
	log_fail "Incorrect space for anyraid vdev: $cap"

log_must zpool attach $TESTPOOL anymirror1-0 $TEST_BASE_DIR/vdev_file.5
log_must zpool rebalance $TESTPOOL anymirror1-0
cap=$(zpool get -Hp -o value size $TESTPOOL)
[[ "$cap" -eq $((18 * 64 * 1024 * 1024)) ]] || \
	log_fail "Incorrect space for anyraid vdev: $cap"
log_must zpool wait -t anyraid_rebalance,scrub $TESTPOOL
log_must zpool sync $TESTPOOL

cap=$(zpool get -Hp -o value size $TESTPOOL)
[[ "$cap" -eq $((35 * 64 * 1024 * 1024)) ]] || \
	log_fail "Incorrect space for anyraid vdev: $cap"

log_must check_pool_status $TESTPOOL state ONLINE true
log_must is_pool_scrubbed $TESTPOOL true
cksum_count=$(zpool status -v $TESTPOOL | grep ONLINE | awk 'NF > 2 && $5 != 0' | wc -l)
[[ "$cksum_count" -eq 0 ]] || log_fail "checksum errors detected"

log_pass "Anyraid rebalance works correctly"
