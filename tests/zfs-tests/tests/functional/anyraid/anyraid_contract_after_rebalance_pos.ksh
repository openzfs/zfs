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
# Verify that contraction works correctly on a pool that was recently
# expanded via attach + rebalance. Expand then contract back.
#
# STRATEGY:
# 1. Create anymirror1 pool with 4 disks
# 2. Write data and record checksums
# 3. Attach a 5th disk, rebalance, wait, verify checksums
# 4. Contract the 5th disk back out, wait, verify checksums
# 5. Verify capacity returned to approximately original
# 6. Scrub, verify no errors
#

verify_runnable "global"

cleanup() {
	log_note "DEBUG: cleanup started"
	poolexists $TESTPOOL && destroy_pool $TESTPOOL
	set_tunable64 ANYRAID_MIN_TILE_SIZE 1073741824
	rm -f $TEST_BASE_DIR/vdev_file.{0,1,2,3,4}
}

log_onexit cleanup

log_note "DEBUG: creating sparse files"
log_must truncate -s 768M $TEST_BASE_DIR/vdev_file.{0,1,2,3,4}

log_note "DEBUG: setting tile size to 64MiB"
set_tunable64 ANYRAID_MIN_TILE_SIZE 67108864

log_assert "Expand then contract lifecycle preserves data integrity"

log_note "DEBUG: creating pool with 4 disks"
log_must create_pool $TESTPOOL anymirror1 \
	$TEST_BASE_DIR/vdev_file.{0,1,2,3}

#
# Write data and record checksums.
#
log_note "DEBUG: writing test files and recording checksums"
typeset -i file_count=10
typeset -i idx=0
set -A cksums

while (( idx < file_count )); do
	log_must file_write -o create -b 1048576 -c 1 -d 'R' \
		-f /$TESTPOOL/file.$idx
	cksums[$idx]=$(xxh128digest /$TESTPOOL/file.$idx)
	log_note "DEBUG: file.$idx checksum=${cksums[$idx]}"
	(( idx = idx + 1 ))
done

log_note "DEBUG: recording original capacity"
cap_original=$(zpool get -Hp -o value size $TESTPOOL)
log_note "DEBUG: original capacity=$cap_original"

#
# Phase 1: Expand by attaching a 5th disk and rebalancing.
#
log_note "DEBUG: phase 1 - attaching vdev_file.4 and rebalancing"
log_must zpool attach $TESTPOOL anymirror1-0 $TEST_BASE_DIR/vdev_file.4
log_must zpool rebalance $TESTPOOL anymirror1-0

log_note "DEBUG: waiting for rebalance to complete"
log_must zpool wait -t anyraid_relocate,scrub $TESTPOOL
log_must zpool sync $TESTPOOL

log_note "DEBUG: recording capacity after expansion"
cap_expanded=$(zpool get -Hp -o value size $TESTPOOL)
log_note "DEBUG: expanded capacity=$cap_expanded"
[[ "$cap_expanded" -gt "$cap_original" ]] || \
	log_fail "Capacity did not increase after expansion"

log_note "DEBUG: verifying checksums after expansion"
idx=0
while (( idx < file_count )); do
	newcksum=$(xxh128digest /$TESTPOOL/file.$idx)
	[[ "$newcksum" == "${cksums[$idx]}" ]] || \
		log_fail "Post-expansion checksum mismatch for file.$idx"
	(( idx = idx + 1 ))
done
log_note "DEBUG: expansion checksums verified"

#
# Phase 2: Contract the 5th disk back out (5 -> 4 disks).
#
log_note "DEBUG: phase 2 - contracting vdev_file.4 (5 -> 4 disks)"
log_must zpool contract $TESTPOOL anymirror1-0 \
	$TEST_BASE_DIR/vdev_file.4

log_note "DEBUG: waiting for contraction to complete"
log_must zpool wait -t anyraid_relocate $TESTPOOL
log_must zpool sync $TESTPOOL

log_note "DEBUG: recording capacity after contraction"
cap_contracted=$(zpool get -Hp -o value size $TESTPOOL)
log_note "DEBUG: contracted capacity=$cap_contracted"
[[ "$cap_contracted" -lt "$cap_expanded" ]] || \
	log_fail "Capacity did not decrease after contraction"

log_note "DEBUG: verifying checksums after contraction"
idx=0
while (( idx < file_count )); do
	newcksum=$(xxh128digest /$TESTPOOL/file.$idx)
	[[ "$newcksum" == "${cksums[$idx]}" ]] || \
		log_fail "Post-contraction checksum mismatch for file.$idx"
	(( idx = idx + 1 ))
done
log_note "DEBUG: contraction checksums verified"

#
# Final verification.
#
log_note "DEBUG: running final scrub"
log_must zpool scrub -w $TESTPOOL

log_note "DEBUG: checking pool status"
log_must check_pool_status $TESTPOOL state ONLINE true
cksum_count=$(zpool status -v $TESTPOOL | grep ONLINE | awk 'NF > 2 && $5 != 0' | wc -l)
[[ "$cksum_count" -eq 0 ]] || log_fail "checksum errors detected"

log_pass "Expand then contract lifecycle preserves data integrity"
