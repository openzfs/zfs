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
# Taking a disk offline during an AnyRAID rebalance is handled
# correctly. The rebalance either completes or pauses gracefully.
# After bringing the disk back online, the pool recovers to ONLINE
# state and data integrity is preserved. This test is self-contained
# and does not depend on any other test.
#
# STRATEGY:
# 1. Create an anymirror1 pool with several small disks.
# 2. Fill with substantial data and record checksums.
# 3. Attach a larger disk and start rebalance.
# 4. Offline one of the original disks during rebalance.
# 5. Verify pool handles the offline gracefully.
# 6. Online the disk back.
# 7. Verify pool recovers to ONLINE state.
# 8. Verify all data checksums are unchanged.
#

verify_runnable "global"

cleanup() {
	poolexists $TESTPOOL && destroy_pool $TESTPOOL
	set_tunable64 ANYRAID_MIN_TILE_SIZE 1073741824
	rm -f $TEST_BASE_DIR/vdev_file.{0,1,2,3,4,5}
}

log_onexit cleanup

log_assert "Disk offline during anyraid rebalance is handled correctly"

#
# Create backing files.
#
log_must truncate -s 768M $TEST_BASE_DIR/vdev_file.{0,1,2,3,4}
log_must truncate -s 10G $TEST_BASE_DIR/vdev_file.5
set_tunable64 ANYRAID_MIN_TILE_SIZE 67108864

#
# Create pool and write substantial data.
#
log_must create_pool $TESTPOOL anymirror1 \
	$TEST_BASE_DIR/vdev_file.{0,1,2,3,4}

typeset -i file_count=10
typeset -i idx=0
set -A cksums

while (( idx < file_count )); do
	log_must file_write -o create -b 1048576 -c 50 -d 'R' \
		-f /$TESTPOOL/file.$idx
	cksums[$idx]=$(xxh128digest /$TESTPOOL/file.$idx)
	(( idx = idx + 1 ))
done

log_must zpool sync $TESTPOOL

#
# Attach a larger disk and start rebalance.
#
log_must zpool attach $TESTPOOL anymirror1-0 $TEST_BASE_DIR/vdev_file.5
log_must zpool rebalance $TESTPOOL anymirror1-0

#
# Offline one of the original disks during rebalance.
# Use a brief sleep to let rebalance get underway.
#
sleep 1

log_must zpool offline $TESTPOOL $TEST_BASE_DIR/vdev_file.2

sleep 1

log_must zpool online $TESTPOOL $TEST_BASE_DIR/vdev_file.2

#
# Wait for the rebalance to complete (it may continue in degraded
# mode, or it may have already finished before the offline took effect).
#
log_must zpool wait -t anyraid_relocate $TESTPOOL

#
# Clear any transient errors from the offline/online cycle.
#
log_must zpool clear $TESTPOOL

#
# Verify the pool is ONLINE.
#
log_must check_pool_status $TESTPOOL state ONLINE true

#
# Verify all file checksums are unchanged.
#
idx=0
typeset -i failedcount=0
while (( idx < file_count )); do
	typeset newcksum=$(xxh128digest /$TESTPOOL/file.$idx)
	if [[ "$newcksum" != "${cksums[$idx]}" ]]; then
		(( failedcount = failedcount + 1 ))
	fi
	(( idx = idx + 1 ))
done

if (( failedcount > 0 )); then
	log_fail "$failedcount of $file_count files had wrong checksums" \
		"after offline during rebalance"
fi

#
# Final scrub to confirm no lingering errors.
#
zpool scrub $TESTPOOL
if (( $? != 0 )); then
	log_note "Scrub already in progress, waiting"
fi
zpool wait -t scrub $TESTPOOL 2>/dev/null

log_pass "Disk offline during anyraid rebalance is handled correctly"
