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
# Replacing a disk while an anyraid rebalance is running (or has just
# completed) is handled correctly. The pool recovers to ONLINE state
# and all data remains intact.
#
# STRATEGY:
# 1. Create an anymirror1 vdev with several small disks
# 2. Fill with substantial data and record checksums
# 3. Attach a larger disk
# 4. Start rebalance
# 5. Fail one original disk (truncate to 0)
# 6. Replace the failed disk with a spare
# 7. Wait for all operations to complete (rebalance, resilver, scrub)
# 8. Verify pool returns to ONLINE state
# 9. Verify all data checksums are unchanged
#

verify_runnable "global"

cleanup() {
	zpool destroy $TESTPOOL
	set_tunable64 ANYRAID_MIN_TILE_SIZE 1073741824
	rm -f $TEST_BASE_DIR/vdev_file.{0,1,2,3,4,5}
	rm -f $TEST_BASE_DIR/vdev_spare
}

log_onexit cleanup

log_must truncate -s 768M $TEST_BASE_DIR/vdev_file.{0,1,2,3,4}
log_must truncate -s 10G $TEST_BASE_DIR/vdev_file.5
log_must truncate -s 768M $TEST_BASE_DIR/vdev_spare

set_tunable64 ANYRAID_MIN_TILE_SIZE 67108864

log_assert "Disk replace during anyraid rebalance is handled correctly"

log_must create_pool $TESTPOOL anymirror1 $TEST_BASE_DIR/vdev_file.{0,1,2,3,4}

#
# Write substantial data and record checksums.
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
# Fail one of the original disks by truncating it to zero.
# This simulates a disk failure during or immediately after rebalance.
#
log_must truncate -s 0 $TEST_BASE_DIR/vdev_file.1

#
# Replace the failed disk with a spare. If replace fails during
# active rebalance, wait for rebalance to finish and retry.
#
zpool replace $TESTPOOL $TEST_BASE_DIR/vdev_file.1 $TEST_BASE_DIR/vdev_spare
if [[ $? -ne 0 ]]; then
	log_note "Replace failed during rebalance, waiting and retrying"
	log_must zpool wait -t anyraid_relocate $TESTPOOL
	log_must zpool replace $TESTPOOL $TEST_BASE_DIR/vdev_file.1 $TEST_BASE_DIR/vdev_spare
fi

log_must zpool wait -t anyraid_relocate,resilver,scrub $TESTPOOL
log_must zpool sync $TESTPOOL

#
# Clear any errors from the failed disk so pool can return to ONLINE.
#
log_must zpool clear $TESTPOOL

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
# Verify pool health.
#
log_must check_pool_status $TESTPOOL state ONLINE true

log_pass "Disk replace during anyraid rebalance is handled correctly"
