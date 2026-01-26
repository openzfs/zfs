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
# Verify that taking a disk offline during contraction is handled
# correctly. The offlined disk is different from the one being
# contracted.
#
# STRATEGY:
# 1. Create anymirror1 pool with 6 disks
# 2. Write 500MiB of data and record checksums
# 3. Start contraction to remove disk 5
# 4. Offline disk 0 (a different disk)
# 5. Verify pool goes DEGRADED
# 6. Wait for contraction to complete
# 7. Online disk 0, wait for resilver
# 8. Verify pool returns to ONLINE
# 9. Verify data integrity
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
log_must truncate -s 768M $TEST_BASE_DIR/vdev_file.{0,1,2,3,4,5}

log_note "DEBUG: setting tile size to 64MiB"
set_tunable64 ANYRAID_MIN_TILE_SIZE 67108864

log_assert "Offline disk during contraction is handled correctly"

log_note "DEBUG: creating pool with 6 disks"
log_must create_pool $TESTPOOL anymirror1 \
	$TEST_BASE_DIR/vdev_file.{0,1,2,3,4,5}

#
# Write substantial data and record checksums.
#
log_note "DEBUG: writing 500MiB of test data"
typeset -i file_count=10
typeset -i idx=0
set -A cksums

while (( idx < file_count )); do
	log_note "DEBUG: writing file.$idx (50MiB)"
	log_must file_write -o create -b 1048576 -c 50 -d 'R' \
		-f /$TESTPOOL/file.$idx
	cksums[$idx]=$(xxh128digest /$TESTPOOL/file.$idx)
	log_note "DEBUG: file.$idx checksum=${cksums[$idx]}"
	(( idx = idx + 1 ))
done

#
# Start contraction to remove disk 5.
#
log_note "DEBUG: starting contraction to remove vdev_file.5"
log_must zpool contract $TESTPOOL anymirror1-0 \
	$TEST_BASE_DIR/vdev_file.5

#
# Offline a different disk (disk 0) during contraction.
#
log_note "DEBUG: offlining vdev_file.0 during contraction"
log_must zpool offline $TESTPOOL $TEST_BASE_DIR/vdev_file.0

log_note "DEBUG: checking pool is DEGRADED"
log_must check_pool_status $TESTPOOL state DEGRADED true

#
# Wait for contraction to pause
#
sleep 2

#
# Online the offlined disk and wait for resilver.
#
log_note "DEBUG: onlining vdev_file.0"
log_must zpool online $TESTPOOL $TEST_BASE_DIR/vdev_file.0

log_note "DEBUG: waiting for resilver to complete"
typeset -i wait_count=0
while (( wait_count < 60 )); do
	if check_state $TESTPOOL "" "online"; then
		break
	fi
	sleep 1
	(( wait_count = wait_count + 1 ))
done
log_note "DEBUG: waited $wait_count seconds for ONLINE"

log_must zpool sync $TESTPOOL

#
# Verify pool is ONLINE.
#
log_note "DEBUG: checking pool status"
log_must check_pool_status $TESTPOOL state ONLINE true

#
# Verify all file checksums are unchanged.
#
log_note "DEBUG: verifying file checksums"
idx=0
while (( idx < file_count )); do
	newcksum=$(xxh128digest /$TESTPOOL/file.$idx)
	[[ "$newcksum" == "${cksums[$idx]}" ]] || \
		log_fail "Checksum mismatch for file.$idx: expected=${cksums[$idx]} got=$newcksum"
	(( idx = idx + 1 ))
done

log_pass "Offline disk during contraction is handled correctly"
