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
# Verify that contraction is rejected when a rebalance is already
# in progress (EALREADY).
#
# STRATEGY:
# 1. Create anymirror1 pool with 5 disks
# 2. Write substantial data (500MiB)
# 3. Attach a 6th disk and start rebalance
# 4. Immediately attempt contraction on a different disk
# 5. Verify contract fails (EALREADY)
# 6. Wait for rebalance to complete
# 7. Verify pool health
#

verify_runnable "global"

cleanup() {
	log_note "DEBUG: cleanup started"
	poolexists $TESTPOOL && destroy_pool $TESTPOOL
	set_tunable64 ANYRAID_MIN_TILE_SIZE 1073741824
	rm -f $TEST_BASE_DIR/vdev_file.{0,1,2,3,4,5}
}

log_onexit cleanup

log_note "DEBUG: creating sparse files"
log_must truncate -s 768M $TEST_BASE_DIR/vdev_file.{0,1,2,3,4}
log_must truncate -s 10G $TEST_BASE_DIR/vdev_file.5

log_note "DEBUG: setting tile size to 64MiB"
set_tunable64 ANYRAID_MIN_TILE_SIZE 67108864

log_assert "Contraction is rejected while a rebalance is in progress"

log_note "DEBUG: creating pool with 5 disks"
log_must create_pool $TESTPOOL anymirror1 \
	$TEST_BASE_DIR/vdev_file.{0,1,2,3,4}

#
# Write substantial data so the rebalance takes measurable time.
#
log_note "DEBUG: writing 500MiB of test data"
typeset -i file_count=10
typeset -i idx=0

while (( idx < file_count )); do
	log_note "DEBUG: writing file.$idx (50MiB)"
	log_must file_write -o create -b 1048576 -c 50 -d 'R' \
		-f /$TESTPOOL/file.$idx
	(( idx = idx + 1 ))
done

#
# Attach a new disk and start rebalance.
#
log_note "DEBUG: attaching vdev_file.5 and starting rebalance"
log_must zpool attach $TESTPOOL anymirror1-0 $TEST_BASE_DIR/vdev_file.5
log_must zpool rebalance $TESTPOOL anymirror1-0

#
# Immediately attempt contraction on a different disk.
# This should fail because a relocate is already in progress.
#
log_note "DEBUG: attempting contraction while rebalance is in progress"
log_mustnot zpool contract $TESTPOOL anymirror1-0 \
	$TEST_BASE_DIR/vdev_file.0
log_note "DEBUG: contraction correctly rejected (EALREADY)"

#
# Wait for rebalance to complete.
#
log_note "DEBUG: waiting for rebalance to complete"
log_must zpool wait -t anyraid_relocate,scrub $TESTPOOL
log_must zpool sync $TESTPOOL

#
# Verify pool health.
#
log_note "DEBUG: checking pool status"
log_must check_pool_status $TESTPOOL state ONLINE true
cksum_count=$(zpool status -v $TESTPOOL | grep ONLINE | awk 'NF > 2 && $5 != 0' | wc -l)
[[ "$cksum_count" -eq 0 ]] || log_fail "checksum errors detected"

log_pass "Contraction is rejected while a rebalance is in progress"
